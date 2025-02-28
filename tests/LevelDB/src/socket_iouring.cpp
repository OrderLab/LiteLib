#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <bpf/libbpf.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <event2/event.h>
#include <event2/util.h>
#include <liburing.h>
#include <fcntl.h>

#define SO_ATTACH_BPF 50
#define SO_DETACH_BPF 27
#define BUFFER_SIZE 65536
#define SERVER_PORT 6379
#define QUEUE_DEPTH 64
#define BUFFER_SIZE 2048
#define BATCH_SIZE 8

static int keep_running = 1;

void signal_handler(int signum) {
    keep_running = 0;
}

int is_resp_message(unsigned char *data, int len) {
    if (len < 1)
        return 0;

    char resp_marker = data[0];
    if (resp_marker == '+' || resp_marker == '-' || resp_marker == ':' ||
            resp_marker == '$' || resp_marker == '*')
        return 1;
    else return 0;
}

struct parse_result{
    unsigned char* payload;
    int len;
    bool request_dir;
    // connection
};

bool check_dir(char* src_ip, uint16_t sport , char* dst_ip,uint16_t dport ){
    if(sport==SERVER_PORT) return false;
    else if(dport==SERVER_PORT) return true;
    return false;

}

struct parse_result parse_tcp_data(unsigned char *buffer, int size) {
    struct ethhdr *eth = (struct ethhdr *)buffer;
    struct parse_result result = {0};
    if (ntohs(eth->h_proto) != ETH_P_IP)
        return result;

    struct iphdr *ip = (struct iphdr *)(buffer + sizeof(struct ethhdr));
    if (ip->protocol != IPPROTO_TCP)
        return result;

    struct tcphdr *tcp = (struct tcphdr *)(buffer + sizeof(struct ethhdr) + 
                                         (ip->ihl * 4));

    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ip->saddr), src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip->daddr), dst_ip, INET_ADDRSTRLEN);

    
    int ip_header_len = ip->ihl * 4;   // Length of IP header in bytes
    int tcp_header_len = tcp->doff * 4; // Length of TCP header in bytes
    int headers_total_len = sizeof(struct ethhdr) + ip_header_len + tcp_header_len;

    if (size > headers_total_len) {
        int payload_size = size - headers_total_len;
        unsigned char *payload = buffer + headers_total_len;
        printf("new tcp data, %s:%d -> %s:%d\n", 
           src_ip, ntohs(tcp->source),
           dst_ip, ntohs(tcp->dest));
        result.request_dir = check_dir(src_ip, ntohs(tcp->source), dst_ip, ntohs(tcp->dest));
        result.payload = payload;
        result.len = payload_size;
        fwrite(payload, sizeof(char), payload_size, stdout);
        printf("\n");
        return result;
    } 
    return result;
}

void read_callback(evutil_socket_t fd, short what, void *arg_conn) {
    unsigned char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    // Connection<Application, Request, Response,
    //             ConnectionInfo, CacheKey, CacheEntry>* conn = static_cast<ConnectionInstance*>(arg_conn);

    ssize_t n = recvfrom(fd, buffer, sizeof(buffer) - 1, 0, 
                         (struct sockaddr *)&sender_addr, &addr_len);

    if (n > 0) {
        struct parse_result result = parse_tcp_data(buffer, n);
        if (result.request_dir){
            //handle request
            // printf("Handle request\n");
            // const auto res = conn->request_->Deserialize(result.payload, result.payload + result.len);
            // if (!conn->lite_core_.HandleRequest(
            //         std::move(conn->request_), conn->extra_app_info_,
            //         conn->pending_requests_, conn->client_fd_, conn->backend_fd_,
            //         &conn->cache_, &conn->logger_, true)) {
            //     delete conn;
            //     return;
            // }
        } else{
            //handle response
            // printf("Handle response\n");
            // const auto res = conn->response_->Deserialize(result.payload, result.payload + result.len);
            // if (!conn->lite_core_.HandleResponse(
            //         std::move(conn->response_), conn->extra_app_info_,
            //         conn->pending_requests_, conn->client_fd_, &conn->cache_,
            //         true)) {
            //     delete conn;
            //     return;
            // }
        }
    } else if (n == 0) {
        printf("Connection closed by peer\n");
        // event_base_loopbreak((struct event_base *)arg); // Break the event loop
    } else {
        perror("recvfrom error");
    }
}



uint16_t htons_custom(uint16_t i) {
    return (i << 8) | (i >> 8);
}

int main() {
    struct bpf_object *obj;
    struct bpf_program *prog;
    struct io_uring ring;
    int err;
    struct __kernel_timespec timeout = {
        .tv_sec = 1,
        .tv_nsec = 0,
    };
    

    // Load BPF program
    obj = bpf_object__open("/home/rishika/cascade/tests/LevelDB/src/socket.bpf.o");
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "Error opening BPF object\n");
        return 1;
    }

    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "Error loading BPF object\n");
        return 1;
    }

    prog = bpf_object__find_program_by_name(obj, "socket__filter_tcp");
    if (!prog) {
        fprintf(stderr, "Error finding BPF program\n");
        return 1;
    }

    // Create raw socket
    int sock_fd = socket(AF_PACKET, SOCK_RAW, htons_custom(ETH_P_ALL));
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }



    // Attach BPF program
    int prog_fd = bpf_program__fd(prog);
    if (setsockopt(sock_fd, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0) {
        perror("Error attaching BPF program");
        close(sock_fd);
        return 1;
    }

    int flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);


    // Initialize io_uring
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        close(sock_fd);
        return 1;
    }

    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    // evutil_make_socket_nonblocking(sock_fd);

    char buffers[QUEUE_DEPTH][BUFFER_SIZE];
    struct iovec iovecs[QUEUE_DEPTH];
    for (int i = 0; i < QUEUE_DEPTH; i++) {
        iovecs[i].iov_base = buffers[i];
        iovecs[i].iov_len = BUFFER_SIZE;
    }

    for (int i = 0; i < QUEUE_DEPTH; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            fprintf(stderr, "Failed to get SQE\n");
            break;
        }
        io_uring_prep_readv(sqe, sock_fd, &iovecs[i], 1, 0);
        io_uring_sqe_set_data(sqe, &iovecs[i]);
    }

    if (io_uring_submit(&ring) < 0) {
        perror("io_uring_submit");
        goto cleanup;
    }

    
    printf("tracing...\n");

    


    // unsigned char buffer[BUFFER_SIZE];

    // Connection new_connection;
    // struct event *read_event = event_new(base, sock_fd, EV_READ | EV_PERSIST, read_callback, NULL);
    // if (!read_event) {
    //     fprintf(stderr, "Could not create event\n");
    //     close(sock_fd);
    //     event_base_free(base);
    //     return 1;
    // }

    // Add the event
    // event_add(read_event, NULL);

    // Run the event loop
    // event_base_dispatch(base);
    

    while (keep_running) {
        struct io_uring_cqe *cqes[BATCH_SIZE];
        int ret = io_uring_wait_cqes(&ring, cqes, BATCH_SIZE, &timeout, NULL);
        if (ret < 0) {
            // perror("io_uring_wait_cqe_batch");
            continue;
        }

        for (int i = 0; i < BATCH_SIZE; i++) {
            struct io_uring_cqe *cqe = cqes[i];
            if (!cqe) break; // No more CQEs in this batch
            if (cqe->res < 0) {
                fprintf(stderr, "Read error: %s\n", strerror(-cqe->res));
            } else {
                struct iovec *iov = (struct iovec *)io_uring_cqe_get_data(cqe);
                printf("Received packet (%d bytes) from buffer: %p\n", cqe->res, iov->iov_base);
                // printf("Data: %.*s\n", cqe->res, (char *)iov->iov_base);
                unsigned char *data = (unsigned char *)iov->iov_base;

                // printf("Received packet (hex): ");
                // for (int j = 0; j < cqe->res; j++) {
                //     printf("%02x ", data[j]);
                // }
                // printf("\n");
                parse_tcp_data(data, cqe->res);
            }

            io_uring_cqe_seen(&ring, cqe);

            // Re-submit the read request
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe) {
                fprintf(stderr, "Failed to get SQE for re-submission\n");
                break;
            }
            io_uring_prep_readv(sqe, sock_fd, (const struct iovec *)io_uring_cqe_get_data(cqe), 1, 0);
            io_uring_sqe_set_data(sqe, io_uring_cqe_get_data(cqe));
        }

        if (io_uring_submit(&ring) < 0) {
            perror("io_uring_submit");
            break;
        }
    }


    // Cleanup
    cleanup:
        setsockopt(sock_fd, SOL_SOCKET, SO_DETACH_BPF, &prog_fd, sizeof(prog_fd));
        close(sock_fd);
        bpf_object__close(obj);
        // event_free(read_event);
        // event_base_free(base);

        printf("bye bye~\n");
    return 0;
}