#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "socketread.skel.h"

#define MAX_EVENTS 10
#define MAX_PAYLOAD_SIZE 50

static volatile sig_atomic_t keep_running = 1;

struct packet_data {
    __u32 size;       // Packet size
    __u8 direction;   // 0 = incoming, 1 = outgoing
    char data[128];   // Packet payload (first 128 bytes)
};

// Signal handler for graceful termination
void handle_signal(int sig) {
    keep_running = 0;
}

void handle_event(void *ctx, int cpu, void *data, __u32 data_size) {
    struct packet_data *event = (struct packet_data *)data;

    printf("Direction: %s, Packet size: %u, Data: %.*s\n",
           event->direction == 0 ? "Incoming" : "Outgoing",
           event->size,
           event->size < MAX_PAYLOAD_SIZE ? event->size : MAX_PAYLOAD_SIZE,
           event->data);
}

void handle_lost_event(void *ctx, int cpu, __u64 lost_cnt) {
    fprintf(stderr, "Lost %llu events on CPU %d\n", lost_cnt, cpu);
}

int main(int argc, char **argv) {
    struct socketread_bpf *skel = NULL;
    struct ring_buffer *ringbuf = NULL;
    int epoll_fd, ringbuf_fd, err;

    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Load and attach eBPF program
    skel = socketread_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = socketread_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF skeleton: %d\n", err);
        goto cleanup;
    }

    // Get ring buffer FD
    ringbuf_fd = bpf_map__fd(skel->maps.events);
    if (ringbuf_fd < 0) {
        fprintf(stderr, "Failed to get ring buffer FD\n");
        goto cleanup;
    }

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        goto cleanup;
    }

    // Add ring buffer FD to epoll
    struct epoll_event event = {
        .events = EPOLLIN,
        .data.fd = ringbuf_fd,
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ringbuf_fd, &event) < 0) {
        perror("epoll_ctl");
        goto cleanup;
    }

    printf("Successfully started! Waiting for events...\n");

    // Poll for events using epoll
    struct epoll_event events[MAX_EVENTS];
    while (keep_running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                // Interrupted by a signal
                break;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == ringbuf_fd) {
                // Consume events from the ring buffer
                ring_buffer__poll(ringbuf, 0 /* timeout */);
            }
        }
    }

    printf("Exiting...\n");

cleanup:
    if (ringbuf)
        ring_buffer__free(ringbuf);
    if (skel)
        socketread_bpf__destroy(skel);
    if (epoll_fd >= 0)
        close(epoll_fd);

    return 0;
}
