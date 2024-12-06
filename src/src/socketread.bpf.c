
// #include "vmlinux.h"
#include <linux/types.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_core_read.h>

// Structure to send data to user space
struct packet_data {
    __u32 size;       // Packet size
    __u8 direction;   // 0 = incoming, 1 = outgoing
    char data[128];   // Packet payload (first 128 bytes)
};

// Ring buffer to send events to user space
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 12); // 4 KB buffer
} events SEC(".maps");

// Function to determine packet direction for TCP
static __u8 get_packet_direction(__u16 src_port, __u16 dest_port, __u16 target_port) {
    if (dest_port == target_port) {
        return 0; // Incoming packet (to the target port)
    }
    if (src_port == target_port) {
        return 1; // Outgoing packet (from the target port)
    }
    return 2; // Unmatched
}

// Function to check if the packet contains a RESP message
static __u8 is_resp_message(char *data, __u32 len) {
    if (len < 1)
        return 0;

    char resp_marker = data[0];
    if (resp_marker == '+' || resp_marker == '-' || resp_marker == ':' ||
            resp_marker == '$' || resp_marker == '*')
        return 1;
    else return 0;
}

// static void *(*bpf_skb_load_bytes)(const struct __sk_buff *, __u32,
//                                    void *, __u32) =
//         (void *) BPF_FUNC_skb_load_bytes;

SEC("socket")
int socket_filter(struct __sk_buff *skb) {
    // void *data = (void *)(long)skb->data;

    struct ethhdr eth_hdr;
    if (bpf_skb_load_bytes(skb, 0, &eth_hdr, sizeof(eth_hdr)) < 0)
	    return 0;
	if (bpf_ntohs(eth_hdr.h_proto) != ETH_P_IP)
		return 0;

    struct iphdr ip_hdr;
	if (bpf_skb_load_bytes(skb, ETH_HLEN, &ip_hdr, sizeof(ip_hdr)) < 0)
        return 0;

    __u16 target_port = 6379; // Specify your desired port

    // Check for TCP packets
    if (ip_hdr.protocol == IPPROTO_TCP) {
        struct tcphdr tcp_hdr;
        if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr), &tcp_hdr, sizeof(tcp_hdr)) < 0)
            return 0;

        __u8 direction = get_packet_direction(
            bpf_ntohs(tcp_hdr.source), 
            bpf_ntohs(tcp_hdr.dest), 
            target_port
        );
        if (direction == 2) // Skip packets not matching the target port
            return 0;

        // Check if the payload contains a RESP message
        __u32 payload_offset = ETH_HLEN + sizeof(struct iphdr) + sizeof(struct tcphdr);
        __u64 payload_len = skb->len - payload_offset;
        if (payload_len < 0)
            return 0;
        if (payload_len > 120)
            payload_len = 120;

        char payload[120] = {};

        if (bpf_skb_load_bytes(skb, payload_offset, payload, payload_len) < 0)
            return 0;
        
        if (!is_resp_message(payload, payload_len))
            return 0;

        // Create an event for user space
        struct packet_data *event = bpf_ringbuf_reserve(&events, sizeof(struct packet_data), 0);
        if (!event)
            return 0;
        // bpf_printk("Packet data: %s", payload);
        // event->pid = bpf_get_current_pid_tgid() >> 32;
        event->size = skb->len;
        event->direction = direction;
        bpf_skb_load_bytes(skb, payload_offset, event->data, payload_len);
            
        // bpf_probe_read_kernel(&event->data, sizeof(event->data), data + payload_offset);

        bpf_ringbuf_submit(event, 0);
    }

    return 0; // Pass the packet to the application
}

char LICENSE[] SEC("license") = "GPL";
