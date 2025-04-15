#define BPF_NO_GLOBAL_DATA
#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

#define READ_RETRY_COUNT 10

#define SERVER_PORT 6379
#define ACCEPT 1
#define CLOSE 2

#define MAX_MSG_SIZE 256
#define MAX_POOLING_CONN (1 << 12)

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

// Define a BPF ring buffer map for passing connection events
struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1 << 16);  // 64KB ring buffer
} conn_ringbuf SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 1);  // element 0: application mode
  __type(key, __u32);      // Array index
  __type(value, __u32);    // Counter value
} emergency_mode SEC(".maps");

// Hook for accepting new TCP connections
SEC("kretprobe/inet_csk_accept")
int bpf_call_inet_csk_accept(struct pt_regs *ctx) {
  // bpf_printk("[%d] inet_csk_accept: ctx: %p\n", __LINE__, ctx);
  struct sock *sk = (struct sock *)PT_REGS_RC(ctx);
  if (!sk) return 0;

  struct sock_common sc;
  int err = bpf_core_read(&sc, sizeof(sc), &sk->__sk_common);
  if (err < 0) {
    bpf_printk("[%d] inet_csk_accept: bpf_core_read failed (err: %d)\n",
               __LINE__, err);
    return 0;
  }

  __u32 saddr = sc.skc_rcv_saddr;
  __u16 sport = sc.skc_num;
  __u32 daddr = sc.skc_daddr;
  __u16 dport = bpf_ntohs(sc.skc_dport);

  if (sport != SERVER_PORT) return 0;

  __u32 key = 0;
  __u32 *value = bpf_map_lookup_elem(&emergency_mode, &key);
  __u32 emergency_mode_value = value ? (*value) : 0;
  if (emergency_mode_value == 1) {
    bpf_printk("[%d] skip bpf_call_inet_csk_accept: emergency mode\n", __LINE__);
    return 0;
  }

  struct connection_event *event =
      bpf_ringbuf_reserve(&conn_ringbuf, sizeof(struct connection_event), 0);
  if (!event) {
    bpf_printk("[%d] accept event ringbuf reserve failed\n", __LINE__);
    return 0;
  }

  event->header.kind = ACCEPT;
  event->socket.proto = 6;  // TCP
  event->socket.saddr = saddr;
  event->socket.sport = sport;
  event->socket.daddr = daddr;
  event->socket.dport = dport;
  event->socket.state = sc.skc_state;

  bpf_printk("[%d] bpf_call_inet_csk_accept: remote_addr: %pI4, remote_port: %d\n",
             __LINE__, &daddr, dport);
  bpf_ringbuf_submit(event, 0);
  return 0;
}

// Hook for closing TCP connections
SEC("kprobe/tcp_close")
int bpf_call_tcp_close(struct pt_regs *ctx) {
  // bpf_printk("[%d] tcp_close: ctx: %p\n", __LINE__, ctx);
  struct sock *sk = (struct sock *)PT_REGS_PARM1(ctx);
  if (!sk) return 0;
  struct sock_common sc;
  int err = bpf_core_read(&sc, sizeof(sc), &sk->__sk_common);
  if (err < 0) {
    bpf_printk("[%d] tcp_close: bpf_core_read failed (err: %d)\n", __LINE__,
               err);
    return 0;
  }
  __u32 saddr = sc.skc_rcv_saddr;
  __u16 sport = sc.skc_num;
  __u32 daddr = sc.skc_daddr;
  __u16 dport = bpf_ntohs(sc.skc_dport);

  if (sport != SERVER_PORT) return 0;

  __u32 key = 0;
  __u32 *value = bpf_map_lookup_elem(&emergency_mode, &key);
  __u32 emergency_mode_value = value ? (*value) : 0;
  if (emergency_mode_value == 1) {
    bpf_printk("[%d] skip bpf_call_tcp_close: emergency mode\n", __LINE__);
    return 0;
  }

  // bpf_printk("Closing connection: saddr=%pI4 sport=%d -> dport=%d\n",
  //            &saddr, sport, dport);
  struct connection_event *event =
      bpf_ringbuf_reserve(&conn_ringbuf, sizeof(struct connection_event), 0);
  if (!event) {
    bpf_printk("[%d] close event ringbuf reserve failed (err: %d)\n", __LINE__,
               err);
    return 0;
  }

  event->header.kind = CLOSE;
  event->socket.proto = 6;  // TCP
  event->socket.saddr = saddr;
  event->socket.sport = sport;
  event->socket.daddr = daddr;
  event->socket.dport = dport;
  event->socket.state = sc.skc_state;

  bpf_ringbuf_submit(event, 0);
  bpf_printk("[%d] bpf_call_tcp_close: remote_addr: %pI4, remote_port: %d\n",
             __LINE__, &daddr, dport);
  return 0;
}

struct conn_id {
  u32 remote_addr;
  u16 remote_port;
} __attribute__((packed));

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, MAX_POOLING_CONN);
  __type(key, struct conn_id);
  __type(value, __u32);
} seq_tracker SEC(".maps");

struct socket_data_event_t {
  u32 remote_addr;
  u16 remote_port;
  bool is_read;
  unsigned int msg_size;
  char msg[MAX_MSG_SIZE];
  u32 seq_num;
} __attribute__((packed));

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1 << 26);
} msgs_ringbuf SEC(".maps");

static inline bool is_resp_connection(const char *line_buffer,
                                      u64 bytes_count) {
  if (bytes_count < 1) {
    return 0;
  }
  char resp_marker = line_buffer[0];
  return (resp_marker == '+' || resp_marker == '-' || resp_marker == ':' ||
          resp_marker == '$' || resp_marker == '*');

  // if (bpf_strncmp(line_buffer, 1, "*") != 0 && bpf_strncmp(line_buffer, 1,
  // "+") != 0 && bpf_strncmp(line_buffer, 1, "-") != 0 )
  // {
  //     return 0;
  // }
  // return 1;
}

struct sock_key_t {
  __u32 remote_ip;
  __u32 remote_port;
};

struct {
  __uint(type, BPF_MAP_TYPE_SOCKHASH);
  __uint(max_entries, MAX_POOLING_CONN);
  __type(key, struct sock_key_t);
  __type(value, __u64);
} sock_hash SEC(".maps");

SEC("sockops")
int bpf_sockops_monitor(struct bpf_sock_ops *skops) {
  u64 err = 0;
  u32 local_port = 0;
  struct sock_key_t key;

  switch (skops->op) {
    case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
      local_port = skops->local_port;
      bpf_printk("[%d] accept sockops: local_port: %d\n", __LINE__, local_port);

      if (local_port != 6379) return 0;
      key.remote_port = bpf_ntohs(skops->remote_port >> 16);
      key.remote_ip = skops->remote_ip4;

      err = bpf_sock_hash_update(skops, &sock_hash, &key, BPF_ANY);
      if (err != 0) {
        bpf_printk(
            "[%d] accept sockops: bpf_sock_hash_update failed (err: %d), remote_addr: "
            "%pI4, remote_port: %d\n",
            __LINE__, err, &key.remote_ip, key.remote_port);
      } else {
        bpf_printk(
            "[%d] accept sockops: bpf_sock_hash_update success: remote_addr: %pI4, "
            "remote_port: %d\n",
            __LINE__, &key.remote_ip, key.remote_port);
      }

      break;
    case BPF_SOCK_OPS_STATE_CB:
      if (skops->args[1] == TCP_CLOSE) {
        local_port = skops->local_port;
        bpf_printk("[%d] close sockops: local_port: %d\n", __LINE__,
                   local_port);

        if (local_port != 6379) return 0;

        key.remote_ip = skops->remote_ip4;
        key.remote_port = bpf_ntohs(skops->remote_port >> 16);

        err = bpf_map_delete_elem(&sock_hash, &key);
        if (err != 0) {
          bpf_printk(
              "[%d] close sockops: bpf_map_delete_elem failed (err: %d), "
              "remote_addr: %pI4, remote_port: %d\n",
              __LINE__, err, &key.remote_ip, key.remote_port);
        } else {
          bpf_printk("[%d] close sockops: remote_addr: %pI4, remote_port: %d\n",
                     __LINE__, &key.remote_ip, key.remote_port);
        }
      }
      break;
  }

  return 0;
}

SEC("sk_skb/stream_verdict")
int handle_redis_request(struct __sk_buff *skb) {
  __u32 key = 0;
  __u32 *value = bpf_map_lookup_elem(&emergency_mode, &key);
  __u32 emergency_mode_value = value ? (*value) : 0;
  if (emergency_mode_value == 1) {
    bpf_printk("[%d] skip handle_redis_request: emergency mode\n", __LINE__);
    return SK_PASS;
  }

  //   bpf_printk("[%d] handle_redis_request: skb->len: %d\n", __LINE__,
  //   skb->len);
  __u32 len = skb->len;
  if (len == 0 || len > MAX_MSG_SIZE) {
    if (len == 0) {
      bpf_printk("[%d] handle_redis_request: len is 0\n", __LINE__);
    } else {
      bpf_printk("[%d] handle_redis_request too long: len: %d\n", __LINE__, len);
    }
    return SK_PASS;
  }

  struct socket_data_event_t *event =
      bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(*event), 0);
  if (!event) {
    bpf_printk("[%d] handle_redis_request: bpf_ringbuf_reserve failed\n",
               __LINE__);
    return SK_PASS;
  }

  event->remote_addr = skb->remote_ip4;
  event->remote_port = bpf_ntohs(skb->remote_port >> 16);
  event->is_read = true;
  event->msg_size = len;
  // bpf_printk("[%d] handle_redis_response: remote_addr: %pI4, remote_port:
  // %d\n", __LINE__, &event->remote_addr, event->remote_port);

  u64 err = bpf_skb_load_bytes(skb, 0, &event->msg, len);
  if (err < 0) {
    bpf_ringbuf_discard(event, 0);
    bpf_printk(
        "[%d] handle_redis_request: bpf_skb_load_bytes failed (err: %d)\n",
        __LINE__, err);
    return SK_PASS;
  }

  struct conn_id id = {.remote_addr = event->remote_addr,
                       .remote_port = event->remote_port};
  u32 *seq = bpf_map_lookup_elem(&seq_tracker, &id);
  if (seq) {
    event->seq_num = *seq;
    (*seq)++;
  } else {
    event->seq_num = 0;
    u32 init = 1;
    err = bpf_map_update_elem(&seq_tracker, &id, &init, BPF_ANY);
    if (err != 0) {
      bpf_printk(
          "[%d] handle_redis_request: bpf_map_update_elem failed (err: "
          "%d)\n",
          __LINE__, err);
    }
  }

  // bpf_printk("[%d] handle_redis_request: %s\n", __LINE__, event->msg);

  bpf_ringbuf_submit(event, 0);
  return SK_PASS;
}

SEC("sk_msg")
int handle_redis_response_sk_msg(struct sk_msg_md *msg) {
  __u32 key = 0;
  __u32 *value = bpf_map_lookup_elem(&emergency_mode, &key);
  __u32 emergency_mode_value = value ? (*value) : 0;
  if (emergency_mode_value == 1) {
    bpf_printk("[%d] skip handle_redis_response_sk_msg: emergency mode\n", __LINE__);
    return SK_PASS;
  }

  //   bpf_printk("[%d] handle_redis_response_sk_msg: msg->size: %d\n", __LINE__,
  //              msg->size);
  u64 err = bpf_msg_pull_data(msg, 0, MAX_MSG_SIZE, 0);
  if (err < 0) {
    bpf_printk(
        "[%d] handle_redis_response_sk_msg: bpf_msg_pull_data failed (err: "
        "%d)\n",
        __LINE__, err);
    return SK_PASS;
  }

  __u32 len = msg->size;
  if (len == 0 || len > MAX_MSG_SIZE) {
    if (len == 0) {
      bpf_printk("[%d] handle_redis_response_sk_msg: len is 0\n", __LINE__);
    } else {
      bpf_printk("[%d] handle_redis_response_sk_msg too long: len: %d\n", __LINE__,
               len);
    }
    return SK_PASS;
  }

  struct socket_data_event_t *event =
      bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(*event), 0);
  if (!event) {
    bpf_printk("[%d] handle_redis_response_sk_msg: ringbuf reserve failed\n",
               __LINE__);
    return SK_PASS;
  }

  event->remote_addr = msg->remote_ip4;
  event->remote_port = bpf_ntohs(msg->remote_port >> 16);
  event->is_read = false;
  event->msg_size = len;
  // bpf_printk("[%d] handle_redis_response_sk_msg: remote_addr: %pI4,
  // remote_port: %d\n", __LINE__, &event->remote_addr, event->remote_port);

  // https://stackoverflow.com/questions/76537723/how-to-print-message-data-in-sk-msg-hook
  void *data = msg->data;
  for (int i = 0; i < MAX_MSG_SIZE && i < len; i++) {
    if ((void *)msg->data + i + 1 > msg->data_end) break;
    event->msg[i] = *((char *)data + i);
  }

  struct conn_id id = {.remote_addr = event->remote_addr,
                       .remote_port = event->remote_port};
  u32 *seq = bpf_map_lookup_elem(&seq_tracker, &id);
  if (seq) {
    event->seq_num = *seq;
    (*seq)++;
  } else {
    event->seq_num = 0;
    u32 init = 1;
    err = bpf_map_update_elem(&seq_tracker, &id, &init, BPF_ANY);
    if (err != 0) {
      bpf_printk(
          "[%d] handle_redis_response_sk_msg: bpf_map_update_elem failed (err: "
          "%d)\n",
          __LINE__, err);
    }
  }

  // bpf_printk("[%d] handle_redis_response_sk_msg: %s\n", __LINE__, event->msg);

  bpf_ringbuf_submit(event, 0);
  return SK_PASS;
}