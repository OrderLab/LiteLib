#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>

#define ETH_P_IP    0x0800  /* Internet Protocol packet */ // ipv4
#define ETH_HLEN    14      /* Total octets in header */
#define SERVER_PORT 6379
#define MAX_CMD_LEN 16

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

const char *write_commands[] SEC(".rodata") = {
        "SET", "SETEX", "APPEND", "INCR", "INCRBY", "DECR", "DECRBY",
        "LPUSH", "RPUSH", "LPOP", "RPOP", "SADD", "SREM", "HSET", "HDEL",
        "DEL", "FLUSHDB", "FLUSHALL", "EXPIRE", NULL  // NULL-terminated
};

int bpf_strncmp(const char *s1, const char *s2, __u32 n) {
    for (__u32 i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0' || s2[i] == '\0') 
            return 1; // Strings are different
    }
    return 0; // Strings are equal
}

__u8 is_resp_message(char *data, __u32 len) {
    if (!data || len < 1)
        return 0;
    
    char resp_marker = data[0];
    return (resp_marker == '+' || resp_marker == '-' || resp_marker == ':' ||
        resp_marker == '$' || resp_marker == '*');
}

// static __inline int read_pkt_data(struct __sk_buff *skb, int offset, char *buf, int len) {
//     if (len > 64)  // Ensure the buffer size is within verifier limits
//         return -1;

//     char tmp[64];  // Ensure the buffer is aligned and within limits
//     if ()
//         return -1;

//     for (int i = 0; i < len; i++) {
//         buf[i] = tmp[i];
//     }
//     return 0;
// }

// Extract the RESP bulk string (command) from packet data
// static __inline int extract_resp_command(struct __sk_buff *skb, int offset, char *cmd) {
    
//     return cmd_len;
// }

// __u8 is_resp_write_request(struct __sk_buff *skb, int offset){
    
//     return 0;
// }

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

    if (skb->len < ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4 + 1)
        return 0;

    if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4, payload, 1) < 0)
        return 0;

    if (!is_resp_message(payload, 1))
        return 0;
    
    if(tcp_hdr.dest == bpf_htons(SERVER_PORT) ){
        struct connection_key key = {};  // Zero initialize
        key.ip = ip_hdr.saddr;
        key.port = bpf_ntohs(tcp_hdr.source);
        // // __u64 *record = bpf_map_lookup_elem(&write_response, &key);
        // int offset = ETH_HLEN + sizeof(struct iphdr) + tcp_hdr.doff * 4;
        // char header[29]; // To read length prefix ($N\r\n)
        // int payload_size = skb->len - offset;
        // if (payload_size > 29) 
        //     payload_size = 29;
        // if (payload_size <= 0) 
        //     return 0;
        // if (skb->len < offset + payload_size)
        //     return 0;
        // if (bpf_skb_load_bytes(skb, offset, header, payload_size) < 0)
        //     return 0;

        // if (header[0] != '$') // Bulk string must start with $
        //     return 0;

        // int cmd_len = header[1] - '0'; // Convert ASCII to int (assuming single-digit length)
        // if (cmd_len <= 0 || cmd_len >= MAX_CMD_LEN)
        //     return 0;
        // if (skb->len < offset + 3 + cmd_len)
        //     return 0;    
        // if (bpf_skb_load_bytes(skb, offset+3, cmd, cmd_len) < 0) // Skip "$N\r\n"
        //     return 0;

        // cmd[cmd_len] = '\0'; // Null-terminate

        // for (int j = 0; write_commands[j] != NULL; j++) {
        //     if (bpf_strncmp(cmd, write_commands[j], cmd_len) == 0) {
        //         // if (record){
        //         //     *record = 1;
        //         // }
        //         return -1; // Write request detected
        //     }
        // }
        
        // if (record){
        //     *record = 0;
        //     return 0;
        // }
        
    }else{
        struct connection_key key = {};  // Zero initialize
        key.ip = ip_hdr.daddr;
        key.port = bpf_ntohs(tcp_hdr.dest);
        __u64 *record = bpf_map_lookup_elem(&write_response, &key);
        if (record && *record != 1)
            return 0;
    }

    
    // bpf_printk("TCP Resp: %pI4:%d\n", &ip_hdr.saddr, bpf_ntohs(tcp_hdr.source));


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



char _license[] SEC("license") = "GPL";
