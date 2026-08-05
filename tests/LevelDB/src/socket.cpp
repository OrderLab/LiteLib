#include <stdio.h>
#include <iostream>
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
#include <thread>
#include <bpf/bpf.h>
#include "socket.skel.h"
#include "socket.h"

#define SO_ATTACH_BPF 50
#define SO_DETACH_BPF 27
#define BUFFER_SIZE 65536
#define SERVER_PORT 6379
#define ACCEPT 1
#define CLOSE 2

struct event_type_header {
    int kind;  // ACCEPT or CLOSE
};

struct socket_info {
    uint8_t proto;
    uint32_t saddr;
    uint16_t sport;
    uint32_t daddr;
    uint16_t dport;
    uint8_t state;  // TCP connection state
    uint32_t seq, ack_seq;
    uint16_t window_size;
};

struct connection_event {
    struct event_type_header header;
    struct socket_info socket;
};

volatile sig_atomic_t running = 1;

void signal_handler(int signum) {
    running = 0;
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

static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct connection_event *event = (struct connection_event *)data;
    printf("Event received: type=%d, src=%u:%u -> dst=%u:%u\n",
           event->header.kind, event->socket.saddr, event->socket.sport,
           event->socket.daddr, event->socket.dport);
    FILE *fp = fopen("tcp_state_dump.txt", "a");
    if (fp) {
        fprintf(fp, "%u %u %u %u %u %u %u %u\n",
                event->socket.saddr, event->socket.sport,
                event->socket.daddr, event->socket.dport,
                event->socket.state, event->socket.seq,
                event->socket.ack_seq, event->socket.window_size);
        fclose(fp);
    }
    
    return 0;  // Return 0 to indicate success
}

int switch_to_emergency(struct socket_bpf *skel){
    uint32_t key=0;
    uint64_t value;
    if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.emergency), &key, &value) < 0) {
        std::cerr << "Failed to look up map value: " << strerror(errno) << std::endl;
        return -1;
    }
    printf("Intial emergency status: %ld\n", value);

    sleep(3);

    value = 1;
    if (bpf_map_update_elem(bpf_map__fd(skel->maps.emergency), &key, &value, BPF_ANY) < 0) {
        std::cerr << "Failed to update value in map: " << strerror(errno) << std::endl;
        return -1;
    }
    if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.emergency), &key, &value) < 0) {
        std::cerr << "Failed to look up map value: " << strerror(errno) << std::endl;
        return -1;
    }
    printf("Intial emergency status: %ld\n", value);
    return 0;
}

uint16_t htons_custom(uint16_t i) {
    return (i << 8) | (i >> 8);
}

int main() {
    struct socket_bpf *skel;
    struct bpf_program *prog;
    struct ring_buffer *rb;
    int err;
    FILE *fp = fopen("tcp_state_dump.txt", "w");
    fclose(fp);
    struct event_base *base = event_base_new();
    if (!base) {
        fprintf(stderr, "Could not initialize libevent\n");
        return 1;
    }

    skel = socket_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    
    err = socket_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF skeleton\n");
        return 1;
    }

    // Create raw socket
    int sock_fd = socket(AF_PACKET, SOCK_RAW, htons_custom(ETH_P_ALL));
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    int prog_fd = bpf_program__fd(skel->progs.socket__filter_tcp);

    if (setsockopt(sock_fd, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, sizeof(prog_fd)) < 0) {
        perror("Error attaching BPF program");
        close(sock_fd);
        return 1;
    }

    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    evutil_make_socket_nonblocking(sock_fd);

    uint32_t key = 0;
    uint64_t value = 0;
    if (bpf_map_update_elem(bpf_map__fd(skel->maps.emergency), &key, &value, BPF_ANY) < 0) {
        std::cerr << "Failed to set initial value in map: " << strerror(errno) << std::endl;
        return -1;
    }
    std::thread t(switch_to_emergency, skel);

    rb = ring_buffer__new(bpf_map__fd(skel->maps.conn_ringbuf), handle_event, NULL, NULL);
    if (!rb) {
        printf("Failed to create ring buffer\n");
        return 1;
    }

    printf("tracing...\n");

    // unsigned char buffer[BUFFER_SIZE];

    // Connection new_connection;
    struct event *read_event = event_new(base, sock_fd, EV_READ | EV_PERSIST, read_callback, NULL);
    if (!read_event) {
        fprintf(stderr, "Could not create event\n");
        close(sock_fd);
        event_base_free(base);
        return 1;
    }

    // Add the event
    event_add(read_event, NULL);


    // Run the event loop

    while(running){
        event_base_loop(base, EVLOOP_NONBLOCK);
        ring_buffer__poll(rb, 100);
    }

    // Cleanup
    cleanup:
        setsockopt(sock_fd, SOL_SOCKET, SO_DETACH_BPF, &prog_fd, sizeof(prog_fd));
        close(sock_fd);
        socket_bpf__destroy(skel);
        event_free(read_event);
        event_base_free(base);
        t.join();
        printf("bye bye~\n");
    return 0;
}