// Redis eBPF Monitor - Cleaned Up Version
#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

#define SERVER_PORT 6379
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
} __attribute__((packed));

struct connection_event {
    struct event_type_header header;
    struct socket_info socket;
} __attribute__((packed));

struct packet_data {
  u32 len;
  u32 saddr;
  u32 daddr;
  u16 sport;
  u16 dport;
  u32 seq_num;
  char direction;  // 'R' for recv, 'S' for send
  unsigned char data[MAX_PKT_SIZE];
} __attribute__((packed));

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

struct conn_key {
  u64 pid_tgid;
} __attribute__((packed));

struct conn_args {
  struct sock *sk;
  void *user_buf;
};

struct send_args_t {
  struct sock *sk;
  void *user_buf;
  size_t size;
};

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1 << 16);
} msgs_ringbuf SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 1 << 16);
  __type(key, struct conn_key);
  __type(value, struct conn_args);
} active_recv SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 1 << 16);
  __type(key, struct conn_key);
  __type(value, struct send_args_t);
} active_send SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 1 << 16);
  __type(key, u64);
  __type(value, void *);
} tid_to_buf SEC(".maps");

// Add after other map definitions
struct seq_info {
    u32 next_send_seq;
    u32 next_recv_seq;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1 << 16);
    __type(key, struct socket_info);
    __type(value, struct seq_info);
} seq_tracker SEC(".maps");

// Hook for accepting new TCP connections
SEC("kretprobe/inet_csk_accept")
int bpf_call_inet_csk_accept(struct pt_regs *ctx) {
    struct sock *sk = (struct sock *)PT_REGS_RC(ctx);
    if (!sk)
        return 0;

    struct sock_common sc;
    int err = bpf_core_read(&sc, sizeof(sc), &sk->__sk_common);
    if (err < 0) {
        bpf_printk("inet_csk_accept: bpf_core_read failed (err: %d)\n", err);
        return 0;
    }

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
    int err = bpf_core_read(&sc, sizeof(sc), &sk->__sk_common);
    if (err < 0) {
        bpf_printk("tcp_close: bpf_core_read failed (err: %d)\n", err);
        return 0;
    }
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
        bpf_printk("close event ringbuf reserve failed (err: %d)\n", err);
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

    struct socket_info sock_key = {
        .proto = 6,
        .saddr = saddr,
        .sport = sport,
        .daddr = daddr,
        .dport = dport
    };

    // Clean up sequence tracking
    err = bpf_map_delete_elem(&seq_tracker, &sock_key);
    if (err < 0) {
        bpf_printk("seq_tracker map delete elem failed (err: %d)\n", err);
    }

    return 0;
}

// Syscall tracepoints to track userspace buffers
SEC("tracepoint/syscalls/sys_enter_recvfrom")
int trace_enter_recvfrom(struct trace_event_raw_sys_enter *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  void *buf = (void *)ctx->args[1];
  int err = bpf_map_update_elem(&tid_to_buf, &tid, &buf, BPF_ANY);
  if (err < 0) {
    bpf_printk("trace_enter_recvfrom: map update elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvfrom")
int trace_exit_recvfrom(struct trace_event_raw_sys_exit *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  int err = bpf_map_delete_elem(&tid_to_buf, &tid);
  if (err < 0) {
    bpf_printk("trace_exit_recvfrom: map delete elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_enter_recvmsg")
int trace_enter_recvmsg(struct trace_event_raw_sys_enter *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  void *buf = (void *)ctx->args[1];
  int err = bpf_map_update_elem(&tid_to_buf, &tid, &buf, BPF_ANY);
  if (err < 0) {
    bpf_printk("trace_enter_recvmsg: map update elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvmsg")
int trace_exit_recvmsg(struct trace_event_raw_sys_exit *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  int err = bpf_map_delete_elem(&tid_to_buf, &tid);
  if (err < 0) {
    bpf_printk("trace_exit_recvmsg: map delete elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_enter_read")
int trace_enter_read(struct trace_event_raw_sys_enter *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  void *buf = (void *)ctx->args[1];
  int err = bpf_map_update_elem(&tid_to_buf, &tid, &buf, BPF_ANY);
  if (err < 0) {
    bpf_printk("trace_enter_read: map update elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_exit_read")
int trace_exit_read(struct trace_event_raw_sys_exit *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  int err = bpf_map_delete_elem(&tid_to_buf, &tid);
  if (err < 0) {
    bpf_printk("trace_exit_read: map delete elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_enter_sendto")
int trace_enter_sendto(struct trace_event_raw_sys_enter *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  void *buf = (void *)ctx->args[1];
  int err = bpf_map_update_elem(&tid_to_buf, &tid, &buf, BPF_ANY);
  if (err < 0) {
    bpf_printk("trace_enter_sendto: map update elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendto")
int trace_exit_sendto(struct trace_event_raw_sys_exit *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  int err = bpf_map_delete_elem(&tid_to_buf, &tid);
  if (err < 0) {
    bpf_printk("trace_exit_sendto: map delete elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_enter_sendmsg")
int trace_enter_sendmsg(struct trace_event_raw_sys_enter *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  void *buf = (void *)ctx->args[1];
  int err = bpf_map_update_elem(&tid_to_buf, &tid, &buf, BPF_ANY);
  if (err < 0) {
    bpf_printk("trace_enter_sendmsg: map update elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendmsg")
int trace_exit_sendmsg(struct trace_event_raw_sys_exit *ctx) {
  u64 tid = bpf_get_current_pid_tgid();
  int err = bpf_map_delete_elem(&tid_to_buf, &tid);
  if (err < 0) {
    bpf_printk("trace_exit_sendmsg: map delete elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("tracepoint/syscalls/sys_enter_write")
int trace_enter_write(struct trace_event_raw_sys_enter *ctx) {
    __u64 tid = bpf_get_current_pid_tgid();
    void *buf = (void *)ctx->args[1];
    int err = bpf_map_update_elem(&tid_to_buf, &tid, &buf, BPF_ANY);
    if (err < 0) {
        bpf_printk("trace_enter_write: map update elem failed (err: %d)\n", err);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_write")
int trace_exit_write(struct trace_event_raw_sys_exit *ctx) {
    __u64 tid = bpf_get_current_pid_tgid();
    int err = bpf_map_delete_elem(&tid_to_buf, &tid);
    if (err < 0) {
        bpf_printk("trace_exit_write: map delete elem failed (err: %d)\n", err);
    }
    return 0;
}

// TCP-level probes for Redis traffic
SEC("kprobe/tcp_sendmsg")
int BPF_KPROBE(tcp_sendmsg_entry, struct sock *sk, struct msghdr *msg,
               size_t size) {
  if (BPF_CORE_READ(sk, __sk_common.skc_num) != SERVER_PORT) return 0;

  u64 tid = bpf_get_current_pid_tgid();
  void **buf_ptr = bpf_map_lookup_elem(&tid_to_buf, &tid);
  if (!buf_ptr) return 0;

  struct conn_key key = {.pid_tgid = tid};
  struct send_args_t val = {.sk = sk, .size = size, .user_buf = *buf_ptr};

  int err = bpf_map_update_elem(&active_send, &key, &val, BPF_ANY);
  if (err < 0) {
    bpf_printk("tcp_sendmsg: map update elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("kretprobe/tcp_sendmsg")
int BPF_KRETPROBE(tcp_sendmsg_exit, ssize_t ret) {
    if (ret <= 0) return 0;
    u64 tid = bpf_get_current_pid_tgid();
    struct conn_key key = {.pid_tgid = tid};
    struct send_args_t *args = bpf_map_lookup_elem(&active_send, &key);
    if (!args) return 0;

    struct packet_data *p = bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(*p), 0);
    if (!p) goto cleanup;

    struct sock *sk = args->sk;
    struct tcp_sock *tp = bpf_core_cast(sk, struct tcp_sock);

    // Create socket_info key for sequence tracking
    struct socket_info sock_key = {
        .proto = 6,
        .saddr = BPF_CORE_READ(sk, __sk_common.skc_rcv_saddr),
        .sport = BPF_CORE_READ(sk, __sk_common.skc_num),
        .daddr = BPF_CORE_READ(sk, __sk_common.skc_daddr),
        .dport = bpf_ntohs(BPF_CORE_READ(sk, __sk_common.skc_dport))
    };

    // Get or initialize sequence tracking
    struct seq_info new_seq = {0};
    struct seq_info *seq = bpf_map_lookup_elem(&seq_tracker, &sock_key);
    if (!seq) {
        // bpf_printk("tcp_sendmsg: no seq tracking\n");
        new_seq.next_send_seq = BPF_CORE_READ(tp, write_seq) - ret;
        int err = bpf_map_update_elem(&seq_tracker, &sock_key, &new_seq, BPF_ANY);
        if (err < 0) {
            bpf_printk("tcp_sendmsg: seq_tracker map update elem failed (err: %d)\n", err);
        }
        seq = &new_seq;
    }

    // Verify sequence number
    u32 current_seq = BPF_CORE_READ(tp, write_seq) - ret;
    // bpf_printk("send sock_key: proto=%d saddr=%u sport=%d daddr=%u dport=%d",
    //            sock_key.proto, sock_key.saddr, sock_key.sport, sock_key.daddr, sock_key.dport);
    if (current_seq != seq->next_send_seq) {
        bpf_printk("Unexpected send seq: expected %u, got %u, ret: %d",
                   seq->next_send_seq, current_seq, ret);
    // } else {
    //     bpf_printk("expected send seq: %u, got %u, ret: %d",
    //                seq->next_send_seq, current_seq, ret);
    }

    // Update next expected sequence number
    struct seq_info updated_seq = *seq;
    updated_seq.next_send_seq = current_seq + ret;
    // bpf_printk("send updated_seq: %u\n", updated_seq.next_send_seq);
    int err = bpf_map_update_elem(&seq_tracker, &sock_key, &updated_seq, BPF_ANY);
    if (err < 0) {
        bpf_printk("tcp_sendmsg: seq_tracker map update elem failed (err: %d)\n", err);
    }

    // Ensure length is bounded
    size_t len = ret;
    if (len > MAX_PKT_SIZE) {
        len = MAX_PKT_SIZE;
        bpf_printk("tcp_sendmsg: len > MAX_PKT_SIZE\n");
    }

    p->len = len;
    p->direction = 'S';
    p->saddr = BPF_CORE_READ(sk, __sk_common.skc_rcv_saddr);
    p->daddr = BPF_CORE_READ(sk, __sk_common.skc_daddr);
    p->sport = BPF_CORE_READ(sk, __sk_common.skc_num);
    p->dport = bpf_ntohs(BPF_CORE_READ(sk, __sk_common.skc_dport));
    p->seq_num = BPF_CORE_READ(tp, write_seq);

    // Use the bounded length for reading
    if (bpf_probe_read_user(p->data, len, args->user_buf) < 0) {
        bpf_ringbuf_discard(p, 0);
        bpf_printk("tcp_sendmsg: read_user failed\n");
        goto cleanup;
    }

    bpf_ringbuf_submit(p, 0);


cleanup:
    err = bpf_map_delete_elem(&active_send, &key);
    if (err < 0) {
        bpf_printk("tcp_sendmsg: map delete elem failed (err: %d)\n", err);
    }
    return 0;
}

SEC("kprobe/tcp_recvmsg")
int BPF_KPROBE(tcp_recvmsg_entry, struct sock *sk, struct msghdr *msg,
               size_t len, int flags, int *addr_len) {
  if (BPF_CORE_READ(sk, __sk_common.skc_num) != SERVER_PORT) return 0;

  u64 tid = bpf_get_current_pid_tgid();
  void **buf_ptr = bpf_map_lookup_elem(&tid_to_buf, &tid);
  if (!buf_ptr) return 0;

  struct conn_key key = {.pid_tgid = tid};
  struct conn_args val = {.sk = sk, .user_buf = *buf_ptr};

  int err = bpf_map_update_elem(&active_recv, &key, &val, BPF_ANY);
  if (err < 0) {
    bpf_printk("tcp_recvmsg: map update elem failed (err: %d)\n", err);
  }
  return 0;
}

SEC("kretprobe/tcp_recvmsg")
int BPF_KRETPROBE(tcp_recvmsg_exit, ssize_t ret) {
    if (ret <= 0) return 0;
    u64 tid = bpf_get_current_pid_tgid();
    struct conn_key key = {.pid_tgid = tid};
    struct conn_args *args = bpf_map_lookup_elem(&active_recv, &key);
    if (!args) return 0;

    struct packet_data *p = bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(*p), 0);
    if (!p) goto cleanup;

    struct sock *sk = args->sk;
    struct tcp_sock *tp = bpf_core_cast(sk, struct tcp_sock);

    // Create socket_info key for sequence tracking
    struct socket_info sock_key = {
        .proto = 6,
        .saddr = BPF_CORE_READ(sk, __sk_common.skc_rcv_saddr),
        .sport = BPF_CORE_READ(sk, __sk_common.skc_num),
        .daddr = BPF_CORE_READ(sk, __sk_common.skc_daddr),
        .dport = bpf_ntohs(BPF_CORE_READ(sk, __sk_common.skc_dport))
    };

    // Get or initialize sequence tracking
    struct seq_info new_seq = {0};
    struct seq_info *seq = bpf_map_lookup_elem(&seq_tracker, &sock_key);
    if (!seq) {
        // bpf_printk("tcp_recvmsg: no seq tracking\n");
        new_seq.next_recv_seq = BPF_CORE_READ(tp, copied_seq) - ret;
        int err = bpf_map_update_elem(&seq_tracker, &sock_key, &new_seq, BPF_ANY);
        if (err < 0) {
            bpf_printk("tcp_recvmsg: seq_tracker map update elem failed (err: %d)\n", err);
        }
        seq = &new_seq;
    }

    // Verify sequence number
    u32 current_seq = BPF_CORE_READ(tp, copied_seq) - ret;
    // bpf_printk("recv sock_key: proto=%d saddr=%u sport=%d daddr=%u dport=%d",
    //            sock_key.proto, sock_key.saddr, sock_key.sport, sock_key.daddr, sock_key.dport);
    if (current_seq != seq->next_recv_seq) {
        bpf_printk("Unexpected recv seq: expected %u, got %u, ret: %d",
                   seq->next_recv_seq, current_seq, ret);
    // } else {
    //     bpf_printk("expected recv seq: %u, got %u, ret: %d",
    //                seq->next_recv_seq, current_seq, ret);
    }

    // Update next expected sequence number
    struct seq_info updated_seq = *seq;
    updated_seq.next_recv_seq = current_seq + ret;
    // bpf_printk("recv updated_seq: %u\n", updated_seq.next_recv_seq);
    int err = bpf_map_update_elem(&seq_tracker, &sock_key, &updated_seq, BPF_ANY);
    if (err < 0) {
        bpf_printk("tcp_recvmsg: seq_tracker map update elem failed (err: %d)\n", err);
    }


    // Ensure length is bounded
    size_t len = ret;
    if (len > MAX_PKT_SIZE) {
        len = MAX_PKT_SIZE;
        bpf_printk("tcp_recvmsg: len > MAX_PKT_SIZE\n");
    }

    p->len = len;
    p->direction = 'R';
    // intentionally reverse the direction of the packet, the def in kernel is strange
    // after the swapping the source is the client, and the destination is the server
    p->daddr = BPF_CORE_READ(sk, __sk_common.skc_rcv_saddr);
    p->saddr = BPF_CORE_READ(sk, __sk_common.skc_daddr);
    p->dport = BPF_CORE_READ(sk, __sk_common.skc_num);
    p->sport = bpf_ntohs(BPF_CORE_READ(sk, __sk_common.skc_dport));
    p->seq_num = BPF_CORE_READ(tp, copied_seq);

    // Use the bounded length for reading
    if (bpf_probe_read_user(p->data, len, args->user_buf) < 0) {
        bpf_ringbuf_discard(p, 0);
        bpf_printk("tcp_recvmsg: read_user failed\n");
        goto cleanup;
    }

    bpf_ringbuf_submit(p, 0);


cleanup:
    err = bpf_map_delete_elem(&active_recv, &key);
    if (err < 0) {
        bpf_printk("tcp_recvmsg: map delete elem failed (err: %d)\n", err);
    }
    return 0;
}
