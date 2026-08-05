#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>

#define ETH_P_IP    0x0800  /* Internet Protocol packet */ // ipv4
#define ETH_HLEN    14      /* Total octets in header */
#define SERVER_PORT 6379

#define ACCEPT 1
#define CLOSE 2

struct event_type_header {
    int kind;  // ACCEPT or CLOSE
};

struct socket_info {
    u8 proto;
    u32 saddr;
    u16 sport;
    u32 daddr;
    u16 dport;
    u8 state;  // TCP connection state
    u32 seq, ack_seq;
    u16 window_size;
};

struct connection_event {
    struct event_type_header header;
    struct socket_info socket;
};

// Define a BPF ring buffer map for passing connection events
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 16);  // 64KB ring buffer
} conn_ringbuf SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);  // Only one element to act as the static variable
    __type(key, __u32);      // Array index
    __type(value, __u64);    // Counter value
} emergency SEC(".maps");

__u8 is_resp_message(char *data, __u32 len) {
    if (len < 1)
        return 0;

    char resp_marker = data[0];
    return (resp_marker == '+' || resp_marker == '-' || resp_marker == ':' ||
            resp_marker == '$' || resp_marker == '*');
}

SEC("socket")
int socket__filter_tcp(struct __sk_buff *skb)
{
    u16 h_proto;
    if (bpf_skb_load_bytes(skb, offsetof(struct ethhdr, h_proto), &h_proto,
                           sizeof(h_proto)) < 0)
        return 0;
    if (bpf_ntohs(h_proto) != ETH_P_IP) // not ipv4
        return 0;

    struct iphdr ip_hdr;
    if (bpf_skb_load_bytes(skb, ETH_HLEN, &ip_hdr, sizeof(ip_hdr)) < 0)
        return 0;
    if (ip_hdr.protocol != IPPROTO_TCP) // not tcp
        return 0;

    struct tcphdr tcp_hdr;
    if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr), &tcp_hdr,
                           sizeof(tcp_hdr)) < 0)
        return 0;
    if (tcp_hdr.dest != bpf_htons(SERVER_PORT) && tcp_hdr.source != bpf_htons(SERVER_PORT))  // filter dest port
        return 0;

    char payload[120] = {0};

    if (skb->len < ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4 + 7)
        return 0;

    if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4, payload, 7) < 0)
        return 0;

    if (!is_resp_message(payload, 7))
        return 0;

    return -1;
}

// Hook for accepting new TCP connections
SEC("kretprobe/inet_csk_accept")
int bpf_call_inet_csk_accept(struct pt_regs *ctx) {
    struct sock *sk = (struct sock *)PT_REGS_RC(ctx);
    if (!sk)
        return 0;

    struct sock_common sc;
    bpf_core_read(&sc, sizeof(sc), &sk->__sk_common);

    __u32 saddr = sc.skc_rcv_saddr;
    __u16 sport = sc.skc_num;
    __u32 daddr = sc.skc_daddr;
    __u16 dport = bpf_ntohs(sc.skc_dport);

    
    if (sport != SERVER_PORT)
        return 0;
    bpf_printk("Opening connection: saddr=%pI4 sport=%d -> daddr=%pI4\n",
               &saddr, sport, &daddr);

    struct connection_event *event = bpf_ringbuf_reserve(&conn_ringbuf, sizeof(struct connection_event), 0);
    if (!event) {
        bpf_printk("accept event ringbuf reserve failed\n");
        return 0;
    }
    u32 write_seq, ack_seq, window_size;
    bpf_core_read(&write_seq, sizeof(write_seq), &((struct tcp_sock *)sk)->write_seq);
    bpf_core_read(&ack_seq, sizeof(ack_seq), &((struct tcp_sock *)sk)->rcv_nxt);
    bpf_core_read(&window_size, sizeof(window_size), &((struct tcp_sock *)sk)->rcv_wnd);
    event->header.kind = ACCEPT;
    event->socket.proto = 6; // TCP
    event->socket.saddr = saddr;
    event->socket.sport = sport;
    event->socket.daddr = daddr;
    event->socket.dport = dport;
    event->socket.state = sc.skc_state;
    event->socket.seq = bpf_ntohl(write_seq);
    event->socket.ack_seq = bpf_ntohl(ack_seq);
    event->socket.window_size = bpf_ntohs(window_size);

    bpf_ringbuf_submit(event, 0);
    return 0;
}

// Hook for closing TCP connections
SEC("kprobe/tcp_close")
int bpf_call_tcp_close(struct pt_regs *ctx) {
    struct sock *sk = (struct sock *)PT_REGS_PARM1(ctx);
    if (!sk)
        return 0;
    struct sock_common sc;
    bpf_core_read(&sc, sizeof(sc), &sk->__sk_common);
    __u32 saddr = sc.skc_rcv_saddr;
    __u16 sport = sc.skc_num;
    __u32 daddr = sc.skc_daddr;
    __u16 dport = bpf_ntohs(sc.skc_dport);
    
    if (sport != SERVER_PORT)
        return 0;
    bpf_printk("Closing connection: saddr=%pI4 sport=%d -> dport=%d\n",
               &saddr, sport, dport);
    struct connection_event *event = bpf_ringbuf_reserve(&conn_ringbuf, sizeof(struct connection_event), 0);
    if (!event) {
        bpf_printk("close event ringbuf reserve failed\n");
        return 0;
    }
    u32 write_seq, ack_seq, window_size;
    bpf_core_read(&write_seq, sizeof(write_seq), &((struct tcp_sock *)sk)->write_seq);
    bpf_core_read(&ack_seq, sizeof(ack_seq), &((struct tcp_sock *)sk)->rcv_nxt);
    bpf_core_read(&window_size, sizeof(window_size), &((struct tcp_sock *)sk)->rcv_wnd);
    event->header.kind = CLOSE;
    event->socket.proto = 6; // TCP
    event->socket.saddr = saddr;
    event->socket.sport = sport;
    event->socket.daddr = daddr;
    event->socket.dport = dport;
    event->socket.state = sc.skc_state;
    event->socket.seq = bpf_ntohl(write_seq);
    event->socket.ack_seq = bpf_ntohl(ack_seq);
    event->socket.window_size = bpf_ntohs(window_size);

    bpf_ringbuf_submit(event, 0);
    return 0;
}

SEC("xdp") 
int redirect_packet(struct __sk_buff *skb) {
    __u32 key = 0;
    __u64 *value;
    bpf_printk("Normal mode\n");
    value = bpf_map_lookup_elem(&emergency, &key);
    if (value) {
        bpf_printk("Emergency mode!\n");
        return XDP_PASS;
    } else{
        bpf_printk("Normal mode\n");
    }

    return XDP_DROP;
}


char _license[] SEC("license") = "GPL";
