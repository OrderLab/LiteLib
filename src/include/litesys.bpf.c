#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>

#define ETH_P_IP    0x0800  /* Internet Protocol packet */ // ipv4
#define ETH_HLEN    14      /* Total octets in header */
#define SERVER_PORT 6379
#define MAX_CMD_LEN 16
#define MAX_PKT_SIZE 4096
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
    u8 state;  
};

struct connection_event {
    struct event_type_header header;
    struct socket_info socket;
};

struct packet_data {
    __u32 len;
    unsigned char data[MAX_PKT_SIZE];
};

// Define a BPF ring buffer map for passing connection events
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 16);  // 64KB ring buffer
} conn_ringbuf SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY); 
    __uint(max_entries, 1);  // element 0: application mode
    __type(key, __u32);      // Array index
    __type(value, __u64);    // Counter value
} mode SEC(".maps");

struct connection_key {
    __u32 ip;
    __u16 port;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10000);  // Support up to 10k concurrent connections
    __type(key, struct connection_key);  // IP and port as composite key
    __type(value, __u64);    // Response tracking value
} write_response SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 16);  // 64KB ring buffer
} msgs_ringbuf SEC(".maps");


struct {
    __uint(type, BPF_MAP_TYPE_SOCKMAP);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} sock_map SEC(".maps");



__u8 is_resp_message(char *data, __u32 len) {
    if (!data || len < 1)
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
    // bpf_printk("Packet received1");

    struct tcphdr tcp_hdr;
    if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr), &tcp_hdr,
                           sizeof(tcp_hdr)) < 0)
        return 0;
    if (tcp_hdr.dest != bpf_htons(SERVER_PORT) && tcp_hdr.source != bpf_htons(SERVER_PORT))  // filter dest port
        return 0;


    char payload[5] = {0};

    if (skb->len < ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4 + 1)
        return 0;

    if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4, payload, 1) < 0)
        return 0;

    if (!is_resp_message(payload, 1))
        return 0;
    // bpf_printk("Size: %d", skb->len);
    
    // bpf_printk("Packet received4");

    // get seq_num from tcp header
    // __u32 cur_seq_num = bpf_ntohl(tcp_hdr.seq);
    // struct connection_key_2 key = {};  // Zero initialize
    //     key.src_ip = ip_hdr.saddr;
    //     key.src_port = bpf_ntohs(tcp_hdr.source);
    //     key.dst_ip = ip_hdr.daddr;
    //     key.dst_port = bpf_ntohs(tcp_hdr.dest);
    // __u32 *last_seq = bpf_map_lookup_elem(&seq_map, &key);

    // if (!last_seq) {
    //     // Store initial sequence number
    //     cur_seq_num = 0;
    //     bpf_map_update_elem(&seq_map, &key, &cur_seq_num, BPF_ANY);
    // } else {
        
    //     if (*last_seq != cur_seq_num) {
    //         bpf_printk("src_ip: %pI4 src_port: %d", &key.src_ip, key.src_port);
    //         bpf_printk("dst_ip: %pI4 dst_port: %d", &key.dst_ip, key.dst_port);
    //         bpf_printk("Out-of-order packet: expected %u, got %u", *last_seq, cur_seq_num);
    //         bpf_printk("Payload: %s", payload);
    //     } else {
    //         bpf_printk("In-order packet: expected %u, got %u", *last_seq, cur_seq_num);
    //     }
    
    // // Update the expected sequence number based on TCP payload size
    //     int payload_len = skb->len - (ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4);
    //     __u32 next_seq = cur_seq_num + (payload_len > 0 ? payload_len : 1); // Ensure it progresses

    //     bpf_map_update_elem(&seq_map, &key, &next_seq, BPF_ANY);
    // }
    
    __u32 pkt_len = (__u32) skb->len;

    // Ensure at least 1 byte is read
    if (pkt_len == 0)
        return 0;
    if (pkt_len > MAX_PKT_SIZE)
        pkt_len = MAX_PKT_SIZE;

    struct packet_data *p = bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(*p), 0);
    if (!p)
        return 0;

    p->len = pkt_len;
    // bpf_printk("Packet received2");
    // Load at least 1 byte to avoid zero-size read
    if (bpf_skb_load_bytes(skb, 0, p->data, pkt_len) < 0) {
        bpf_ringbuf_discard(p, 0);
        return 0;
    }

    bpf_ringbuf_submit(p, 0);
    
    // bpf_printk("Packet received3");
    return 0;
    
    // if(tcp_hdr.dest == bpf_htons(SERVER_PORT) ){
    //     struct connection_key key = {};  // Zero initialize
    //     key.ip = ip_hdr.saddr;
    //     key.port = bpf_ntohs(tcp_hdr.source);
    //     __u64 *record = bpf_map_lookup_elem(&write_response, &key);
    //     // print key
    //     // bpf_printk("key: %pI4:%d", &key.ip, key.port);
    //     int offset = ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4;
    //     char header[29]; // To read length prefix ($N\r\n)
    //     if (skb->len < offset + 4){
    //         if (record){
    //             *record = 0;
    //         } 
    //         return 0;
    //     }
    //     if (bpf_skb_load_bytes(skb, offset, header, 4) < 0){
    //         if (record){
    //             *record = 0;
    //         }
    //         return 0;
    //     }
    //     // bpf_printk("header: %c%c%c", header[1], header[2], header[3]);
    //     if (header[0] != '*' || header[1] != '3' || header[2] != '\r' || header[3] != '\n') {
    //         if (record){
    //             *record = 0;
    //         }
    //         return 0;
    //     }
        
    //     if (skb->len < offset + 4 + 4){
    //         if (record){
    //             *record = 0;
    //         }
    //         return 0;
    //     }
    //     if (bpf_skb_load_bytes(skb, offset + 4, header, 4) < 0){
    //         if (record){
    //             *record = 0;
    //         }
    //         return 0;
    //     }
    //     // bpf_printk("header: %c%c%c", header[1], header[2], header[3]);

    //     if (header[0] != '$' || (header[1] != '3' && header[1] != '6' ) || header[2] != '\r' || header[3] != '\n') {
    //         if (record){
    //             *record = 0;
    //         }
    //         return 0;
    //     }

    //     if (header[1] == '3'){
    //         if (skb->len < offset + 4 + 4 + 3){
    //             if (record){
    //                 *record = 0;
    //             }
    //             return 0;
    //         }
    //         char cmd[3];
    //         if (bpf_skb_load_bytes(skb, offset + 4 + 4, cmd, 3) < 0){
    //             if (record){
    //                 *record = 0;
    //             }
    //             return 0;
    //         }
    //         // bpf_printk("cmd: %c%c%c", cmd[0], cmd[1], cmd[2]);
    //         if (cmd[0] != 'S' && cmd[0] != 's' || cmd[1] != 'E' && cmd[1] != 'e' || cmd[2] != 'T' && cmd[2] != 't'){
    //             if (record){
    //                 *record = 0;
    //             }
    //             return 0;
    //         }
            
    //     }
    //     if (header[1] == '6'){
    //         if (skb->len < offset + 4 + 4 + 6){
    //             if (record){
    //                 *record = 0;
    //             }
    //             return 0;
    //         }
    //         char cmd[6];
    //         if (bpf_skb_load_bytes(skb, offset + 4 + 4, cmd, 6) < 0){
    //             if (record){
    //                 *record = 0;
    //             }
    //             return 0;
    //         }
    //         // bpf_printk("cmd: %c%c%c", cmd[0], cmd[1], cmd[2] );
    //         if (cmd[0] != 'G' && cmd[0] != 'g' || cmd[1] != 'E' && cmd[1] != 'e' || cmd[2] != 'T' && cmd[2] != 't' || cmd[3] != 'S' && cmd[3] != 's' || cmd[4] != 'E' && cmd[4] != 'e' || cmd[5] != 'T' && cmd[5] != 't'){
    //             if (record){
    //                 *record = 0;
    //             }
    //             return 0;
    //         }
    //     }
    //     if (record){
    //         *record = 1;
    //     } else {
    //         __u64 value = 1;
    //         bpf_map_update_elem(&write_response, &key, &value, BPF_ANY);
    //     }
    //     return -1;
    // }else{
    //     struct connection_key key = {};  // Zero initialize
    //     key.ip = ip_hdr.daddr;
    //     key.port = bpf_ntohs(tcp_hdr.dest);
    //     // print key
    //     // bpf_printk("key: %pI4:%d", &key.ip, key.port);
    //     __u64 *record = bpf_map_lookup_elem(&write_response, &key);
    //     if (!record){
    //         return 0;
    //     }
    //     // bpf_printk("record: %d", *record);
    //     if (*record != 1)
    //         return 0;
    //     *record = 0;
    //     return -1;
    // }

    // Print payload data
    // char payload[64];
    // if (skb->len < ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4 + 7){
    //     int payload_len = bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4, payload, 10);
    //     if (payload_len >= 0) {
    //         bpf_printk("Payload: ");
    //         for (int i = 0; i < 10; i++) {
    //             if (i >= 10) break;
    //             if (payload[i] >= 32 && payload[i] <= 126) {
    //                 bpf_printk("%c", payload[i]);
    //             } else {
    //                 bpf_printk("%02X ", payload[i]);
    //             }
    //         }
    //         bpf_printk("\n");
    //     }
    // }

    // if (skb->len >= ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4 + 4){
    //     if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4, payload, 4) < 0)
    //         return 0;
        
    //     bpf_printk("Payload: %s", payload);
        
    // }
        
    

    
    
    // bpf_printk("TCP packet: %pI4:%d \n", &ip_hdr.daddr, bpf_ntohs(tcp_hdr.dest));


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
    // bpf_printk("Opening connection: saddr=%pI4 sport=%d -> daddr=%pI4\n",
    //            &saddr, sport, &daddr);

    struct connection_event *event = bpf_ringbuf_reserve(&conn_ringbuf, sizeof(struct connection_event), 0);
    if (!event) {
        bpf_printk("accept event ringbuf reserve failed\n");
        return 0;
    }
    
    event->header.kind = ACCEPT;
    event->socket.proto = 6; // TCP
    event->socket.saddr = saddr;
    event->socket.sport = sport;
    event->socket.daddr = daddr;
    event->socket.dport = dport;
    event->socket.state = sc.skc_state;

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
    // bpf_printk("Closing connection: saddr=%pI4 sport=%d -> dport=%d\n",
    //            &saddr, sport, dport);
    struct connection_event *event = bpf_ringbuf_reserve(&conn_ringbuf, sizeof(struct connection_event), 0);
    if (!event) {
        bpf_printk("close event ringbuf reserve failed\n");
        return 0;
    }
    
    
    event->header.kind = CLOSE;
    event->socket.proto = 6; // TCP
    event->socket.saddr = saddr;
    event->socket.sport = sport;
    event->socket.daddr = daddr;
    event->socket.dport = dport;
    event->socket.state = sc.skc_state;
    
    bpf_ringbuf_submit(event, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";