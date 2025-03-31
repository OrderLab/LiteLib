#include "vmlinux.h"
// packet_filter.c
// #include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define MAX_PKT_SIZE 4096

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} ringbuf SEC(".maps");

struct packet_data {
    __u32 len;
    char data[MAX_PKT_SIZE];
};

SEC("socket")
int capture_packet(struct __sk_buff *skb) {
    __u32 pkt_len = (__u32) skb->len;

    // Ensure at least 1 byte is read
    if (pkt_len == 0)
        return -1;
    if (pkt_len > MAX_PKT_SIZE)
        pkt_len = MAX_PKT_SIZE;

    struct packet_data *p = bpf_ringbuf_reserve(&ringbuf, sizeof(*p), 0);
    if (!p)
        return -1;

    p->len = pkt_len;

    // Load at least 1 byte to avoid zero-size read
    if (bpf_skb_load_bytes(skb, 0, p->data, pkt_len) < 0) {
        bpf_ringbuf_discard(p, 0);
        return -1;
    }

    bpf_ringbuf_submit(p, 0);
    return -1;
}

char LICENSE[] SEC("license") = "GPL";
