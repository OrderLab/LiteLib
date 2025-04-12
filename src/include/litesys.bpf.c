// Redis eBPF Monitor - Cleaned Up Version
#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

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
    __uint(max_entries, 2);  // element 0: socket fd, element 1: pid
    __type(key, __u32);      // Array index
    __type(value, __u64);    // Counter value
} socket_info SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);  // element 0: application mode
    __type(key, __u32);      // Array index
    __type(value, __u64);    // Counter value
} mode SEC(".maps");


struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1 << 26);
} msgs_ringbuf SEC(".maps");


struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_POOLING_CONN);
    __type(key, __u64);
    __type(value, __u32);
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

    return 0;
}

struct socket_data_event_t
{
  unsigned int pid;
  int fd;
  bool is_connection;
  int socket_fd;
  bool is_read;
  unsigned int msg_size;
  char msg[MAX_MSG_SIZE];
  u32 seq_num;
} __attribute__((packed));

struct conn_id_t
{
    u32 pid;
    int fd;
    __u64 tsid;
};

struct conn_info_t
{
    struct conn_id_t conn_id;
    int listen_fd;
    __s64 wr_bytes;
    __s64 rd_bytes;
    bool is_resp;
};

// A struct describing the event that we send to the user mode upon a new connection.
struct socket_open_event_t
{
    // A unique ID for the connection.
    struct conn_id_t conn_id;
};

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_POOLING_CONN);
    __type(key, __u64);
    __type(value, struct conn_info_t);
} conn_info_map SEC(".maps");


struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_POOLING_CONN);
    __type(key, u64);
    __type(value, struct accept_args_t);
} active_accept_args_map SEC(".maps");


struct accept_args_t {
    int sockfd;       // Listening socket FD from accept() argument
    struct sockaddr* addr;
};

SEC("tracepoint/syscalls/sys_enter_accept")
int sys_enter_accept(struct trace_event_raw_sys_enter *ctx) {
    u64 id = bpf_get_current_pid_tgid();

    bpf_printk("sys_enter_accept tid: %llu\n", id);

    struct accept_args_t accept_args = {};
    accept_args.sockfd = (int)ctx->args[0];  // Get listening socket FD
    accept_args.addr = (struct sockaddr *)ctx->args[1];

    u64 err = bpf_map_update_elem(&active_accept_args_map, &id, &accept_args, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_enter_accept: map update failed (err: %d)\n", err);
    }
    return 0;
}


SEC("tracepoint/syscalls/sys_exit_accept")
int sys_exit_accept(struct trace_event_raw_sys_exit *ctx)
{
    u64 id = bpf_get_current_pid_tgid();

    struct accept_args_t *args =
        bpf_map_lookup_elem(&active_accept_args_map, &id);
    if (args == NULL)
    {
        return 0;
    }
    // bpf_printk("exit_accept accept_args.addr: %llx\n", args->addr);
    int ret_fd = (int)BPF_CORE_READ(ctx, ret);
    if (ret_fd <= 0)
    {
        return 0;
    }

    struct conn_info_t conn_info = {};

    u32 pid = id >> 32;
    conn_info.conn_id.pid = pid;
    conn_info.conn_id.fd = ret_fd;
    conn_info.listen_fd = args->sockfd;
    conn_info.conn_id.tsid = bpf_ktime_get_ns();

    __u32 key = 0;
    __u64 *socket_fd_ptr = bpf_map_lookup_elem(&socket_info, &key);
    if (!socket_fd_ptr) {
        return 0;
    }
    if(*socket_fd_ptr != args->sockfd){
      return 0;
    }
    key = 1;
    __u64 *pid_ptr = bpf_map_lookup_elem(&socket_info, &key);
    if (!pid_ptr) {
        return 0;
    }
    if(*pid_ptr != pid){
      return 0;
    }

    __u64 pid_fd = ((__u64)pid << 32) | (u32)ret_fd;
    u64 err = bpf_map_update_elem(&conn_info_map, &pid_fd, &conn_info, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_exit_accept: map update failed (err: %d)\n", err);
    }

    // struct socket_data_event_t *open_event = bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(struct socket_data_event_t), 0);
    // if (!open_event) {
    //     return 0;
    // }

    // open_event->pid = conn_info.conn_id.pid;
    // open_event->fd = conn_info.conn_id.fd;
    // open_event->socket_fd = conn_info.listen_fd;
    // open_event->is_connection = true;

    // bpf_ringbuf_submit(open_event, 0);
    // bpf_printk("open_event key: %llu\n", pid_fd);

    err = bpf_map_delete_elem(&active_accept_args_map, &id);
    if (err != 0) {
        bpf_printk("sys_exit_accept: map delete failed (err: %d)\n", err);
    }
    return 0;
}

struct data_args_t
{
    __s32 fd;
    const char *buf;
};

struct data_args_array_t
{
    __s32 fd;
    char buf[MAX_MSG_SIZE];
};



struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_POOLING_CONN);
    __type(key, u64);
    __type(value, struct data_args_t);
} active_read_args_map SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_POOLING_CONN);
    __type(key, u64);
    __type(value, struct data_args_t);
} active_write_args_map SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_POOLING_CONN);
    __type(key, u64);
    __type(value, struct data_args_array_t);
} active_write_event_args_map SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_POOLING_CONN);
    __type(key, u64);
    __type(value, struct data_args_array_t);
} active_read_event_args_map SEC(".maps");


static inline bool is_resp_connection(const char *line_buffer, u64 bytes_count)
{
    if (bytes_count < 1)
    {
        return 0;
    }
    char resp_marker = line_buffer[0];
    return (resp_marker == '+' || resp_marker == '-' || resp_marker == ':' ||
        resp_marker == '$' || resp_marker == '*');

    // if (bpf_strncmp(line_buffer, 1, "*") != 0 && bpf_strncmp(line_buffer, 1, "+") != 0 && bpf_strncmp(line_buffer, 1, "-") != 0 )
    // {
    //     return 0;
    // }
    // return 1;
}

static inline void process_data(
                                u64 id, const struct data_args_t *args, u64 bytes_count, bool is_read)
{
    if (args->buf == NULL)
    {
        return;
    }
    u32 pid = id >> 32;

    char line_buffer[1];
    bpf_probe_read(line_buffer, 1, args->buf);
    if (!is_resp_connection(line_buffer, bytes_count) && bytes_count <= MAX_MSG_SIZE)
    {
        // print each character in the buffer
        // for (int i = 0; i < bytes_count; i++) {
        //     if (args->buf[i] < 32 || args->buf[i] > 126) {
        //         bpf_printk("0x%02x ", args->buf[i]);
        //     }  else {
        //         bpf_printk("%c", args->buf[i]);
        //     }
        // }
        bpf_printk("process_data: 0x%02x ", line_buffer[0]);
    }
    struct socket_data_event_t *event = bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(struct socket_data_event_t), 0);
    if (!event) {
        bpf_printk("ringbuf reserve failed\n");
        return;
    }

    __u64 key = args->fd;
    __u32 *seq_num = bpf_map_lookup_elem(&seq_tracker, &key);
    if (!seq_num) {
        event->seq_num = 0;
        __u32 next_seq_num = 1;
        if (bpf_map_update_elem(&seq_tracker, &key, &next_seq_num, BPF_ANY) != 0) {
            bpf_printk("process_data: map update failed\n");
        }
    } else {
        event->seq_num = *seq_num;
        *seq_num = *seq_num + 1;
    }

    event->is_connection = false;
    event->pid = pid;
    event->fd = args->fd;
    event->is_read = is_read;
    if (bytes_count >= MAX_MSG_SIZE)
        bpf_printk("process_data: bytes_count >= MAX_MSG_SIZE\n");
    unsigned int read_size = bytes_count >= MAX_MSG_SIZE ? MAX_MSG_SIZE - 1 : bytes_count;
    event->msg_size = read_size;
    bpf_probe_read(&event->msg, read_size, args->buf);
        // bpf_printk("event->msg: %s\n", event->msg);
    bpf_ringbuf_submit(event, 0);

}

static inline void process_data_2(
                                u64 id, const struct data_args_array_t *args, u64 bytes_count, bool is_read)
{
    if (args->buf == NULL)
    {
        return;
    }
    u32 pid = id >> 32;

    char line_buffer[1];
    bpf_probe_read(line_buffer, 1, args->buf);
    if (!is_resp_connection(line_buffer, bytes_count) && bytes_count <= MAX_MSG_SIZE)
    {
        // print each character in the buffer
        // for (int i = 0; i < bytes_count; i++) {
        //     if (args->buf[i] < 32 || args->buf[i] > 126) {
        //         bpf_printk("0x%02x ", args->buf[i]);
        //     }  else {
        //         bpf_printk("%c", args->buf[i]);
        //     }
        // }
        // bpf_printk("process_data: 0x%02x ", line_buffer[0]);
        // return;
    }
    // bpf_printk("process_data_2: 0x%02x ", line_buffer[0]);
    struct socket_data_event_t *event = bpf_ringbuf_reserve(&msgs_ringbuf, sizeof(struct socket_data_event_t), 0);
    if (!event) {
        bpf_printk("ringbuf reserve failed\n");
        return;
    }

    __u64 key = args->fd;
    __u32 *seq_num = bpf_map_lookup_elem(&seq_tracker, &key);
    if (!seq_num) {
        event->seq_num = 0;
        __u32 next_seq_num = 1;
        if (bpf_map_update_elem(&seq_tracker, &key, &next_seq_num, BPF_ANY) != 0) {
            bpf_printk("process_data: map update failed\n");
        }
    } else {
        event->seq_num = *seq_num;
        *seq_num = *seq_num + 1;
    }

    event->is_connection = false;
    event->pid = pid;
    event->fd = args->fd;
    event->is_read = is_read;
    if (bytes_count >= MAX_MSG_SIZE)
        bpf_printk("process_data write: bytes_count >= MAX_MSG_SIZE\n");
    unsigned int read_size = bytes_count >= MAX_MSG_SIZE ? MAX_MSG_SIZE - 1 : bytes_count;
    event->msg_size = read_size;
    bpf_probe_read(&event->msg, read_size, (void *)args->buf);
        // bpf_printk("event->msg: %s\n", event->msg);
    bpf_ringbuf_submit(event, 0);

}

SEC("tracepoint/syscalls/sys_enter_read")
int sys_enter_read(struct trace_event_raw_sys_enter *ctx)
{
    u64 id = bpf_get_current_pid_tgid();
    u64 fd = (u64)BPF_CORE_READ(ctx, args[0]);
    u32 pid = id >> 32;
    u64 pid_fd = ((u64)pid << 32) | (u64)fd;
    struct conn_info_t *conn_info = bpf_map_lookup_elem(&conn_info_map, &pid_fd);
    // bpf_printk("sys_enter_read pid_fd: %llu, pid: %d\n", pid_fd, pid);
    if (conn_info == NULL)
    {
        return 0;
    }
    // bpf_printk("read: %llu\n", pid_fd);
    struct data_args_t read_args = {};
    read_args.fd = (int)fd;
    read_args.buf = (char *)BPF_CORE_READ(ctx, args[1]);
    struct data_args_t *read_args_prev = bpf_map_lookup_elem(&active_read_args_map, &id);
    if (read_args_prev != NULL)
    {
        bpf_printk("read_args already exists\n");
    }
    u64 err = bpf_map_update_elem(&active_read_args_map, &id, &read_args, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_enter_read: map update failed (err: %d)\n", err);
    }

    return 0;
}


SEC("tracepoint/syscalls/sys_exit_read")
int sys_exit_read(struct trace_event_raw_sys_exit *ctx)
{
    u64 bytes_count = (u64)BPF_CORE_READ(ctx, ret);
    if (bytes_count <= 0)
    {
        return 0;
    }
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_t *read_args = bpf_map_lookup_elem(&active_read_args_map, &id);
    if (read_args == NULL) return 0;
    process_data(id, read_args, bytes_count, true);

    u64 err = bpf_map_delete_elem(&active_read_args_map, &id);
    if (err != 0) {
        bpf_printk("sys_exit_read: map delete failed (err: %d)\n", err);
    }

    return 0;
}

SEC("tracepoint/syscalls/sys_enter_recvmsg")
int sys_enter_recvmsg(struct trace_event_raw_sys_enter *ctx)
{
    u64 id = bpf_get_current_pid_tgid();
    u64 fd = (u64)BPF_CORE_READ(ctx, args[0]);
    u32 pid = id >> 32;
    u64 pid_fd = ((u64)pid << 32) | (u64)fd;
    struct conn_info_t *conn_info = bpf_map_lookup_elem(&conn_info_map, &pid_fd);
    if (conn_info == NULL)
    {
        return 0;
    }
    // bpf_printk("recvmsg: %llu\n", pid_fd);
    struct data_args_t read_args = {};
    read_args.fd = (int)fd;
    read_args.buf = (char *)BPF_CORE_READ(ctx, args[1]);
    struct data_args_t *read_args_prev = bpf_map_lookup_elem(&active_read_args_map, &id);
    if (read_args_prev != NULL)
    {
        bpf_printk("read_args already exists\n");
    }
    u64 err = bpf_map_update_elem(&active_read_args_map, &id, &read_args, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_enter_recvmsg: map update failed (err: %d)\n", err);
    }

    return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvmsg")
int sys_exit_recvmsg(struct trace_event_raw_sys_exit *ctx)
{
    u64 bytes_count = (u64)BPF_CORE_READ(ctx, ret);
    if (bytes_count <= 0)
    {
        return 0;
    }
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_t *read_args = bpf_map_lookup_elem(&active_read_args_map, &id);
    if (read_args == NULL) return 0;
    process_data(id, read_args, bytes_count, true);

    u64 err = bpf_map_delete_elem(&active_read_args_map, &id);
    if (err != 0) {
        bpf_printk("sys_exit_recvmsg: map delete failed (err: %d)\n", err);
    }

    return 0;
}

SEC("kprobe/__x64_sys_recvfrom")
int BPF_KPROBE(handle_recvfrom, struct pt_regs *regs)
{
    // Use BPF_KPROBE macro without parameters, then use the helper macros
    int fd = PT_REGS_PARM1_CORE(regs);
    void *buf = (void *)PT_REGS_PARM2_CORE(regs);
    size_t len = PT_REGS_PARM3_CORE(regs);

    u64 buf_len = (u64)len;
    if (buf_len <= 0 || buf_len > MAX_MSG_SIZE) {
        buf_len = buf_len > MAX_MSG_SIZE ? MAX_MSG_SIZE - 1 : 0;
    }
    
    u64 id = bpf_get_current_pid_tgid();
    u32 pid = id >> 32;
    u64 pid_fd = ((u64)pid << 32) | (u64)fd;
    
    struct conn_info_t *conn_info = bpf_map_lookup_elem(&conn_info_map, &pid_fd);
    if (conn_info == NULL)
    {
        return 0;
    }
    // bpf_printk("recvfrom: %d %p %d\n", fd, buf, len);
    
    struct data_args_t read_args = {};
    u32 read_size = buf_len > MAX_MSG_SIZE - 1 ? MAX_MSG_SIZE - 1 : buf_len;
    
    // Use safe helper function for memory access
    if (read_size > 0) {
        // int ret = bpf_probe_read_user(&read_args.buf, read_size, (void *)buf);
        // bpf_printk("read_size: %s ret: %d\n", read_args.buf, ret);
    }
    read_args.fd = fd;
    read_args.buf = (char *)buf;
    struct data_args_t *read_args_prev = bpf_map_lookup_elem(&active_read_args_map, &id);
    if (read_args_prev != NULL)
    {
        bpf_printk("read_args already exists\n");
    }
    u64 err = bpf_map_update_elem(&active_read_args_map, &id, &read_args, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_enter_recvfrom: map update failed (err: %d)\n", err);
    }

    return 0;
}

SEC("kretprobe/__x64_sys_recvfrom")
int BPF_KRETPROBE(handle_recvfrom_ret, ssize_t ret)
{
    u64 bytes_count = (u64)ret;
    // bpf_printk("recvfrom ret: %d\n", bytes_count);
    if (bytes_count <= 0)
    {
        return 0;
    }
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_t *read_args = bpf_map_lookup_elem(&active_read_args_map, &id);
    if (read_args == NULL) return 0;
    // bpf_printk("read_args: %s\n", read_args->buf);
    process_data(id, read_args, bytes_count, true);

    u64 err = bpf_map_delete_elem(&active_read_args_map, &id);
    if (err != 0) {
        bpf_printk("sys_exit_recvfrom: map delete failed (err: %d)\n", err);
    }

    return 0;
}

SEC("kprobe/ksys_write")
int BPF_KPROBE(handle_write, int fd, const char *buf, size_t count)
{
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_array_t write_args = {};
    write_args.fd = fd;
    // write_args.buf = buf;
    
    // Get buffer length and ensure it's positive
    u64 buf_len = (u64)count;
    if (buf_len <= 0 || buf_len > MAX_MSG_SIZE) {
        buf_len = buf_len > MAX_MSG_SIZE ? MAX_MSG_SIZE - 1 : 0;
    }
    
    u32 pid = id >> 32;
    u64 pid_fd = ((u64)pid << 32) | (u64)write_args.fd;

    struct conn_info_t *conn_info = bpf_map_lookup_elem(&conn_info_map, &pid_fd);
    if (conn_info == NULL)
    {
        return 0;
    }
    // bpf_printk("write: fd=%d count=%d\n", fd, (int)count);
    
    // Use unsigned value for read_size to avoid verifier issues
    u32 read_size = buf_len > MAX_MSG_SIZE - 1 ? MAX_MSG_SIZE - 1 : buf_len;
    
    // Use safe helper function for memory access
    if (buf_len > 0) {
        bpf_probe_read_user(&write_args.buf, buf_len, (void *)buf);
    }
        
    char line_buffer[1];
    bpf_probe_read(line_buffer, 1, write_args.buf);
    // for (int i = 0; i < read_size; i++) {
    //     if (write_args.buf[i] < 32 || write_args.buf[i] > 126) {
    //         bpf_printk("0x%02x ", write_args.buf[i]);
    //     } else {
    //         bpf_printk("%c", write_args.buf[i]);
    //     }
    // }
    if (!is_resp_connection(line_buffer, 1))
    {
        bpf_printk("enter 0x%02x ", line_buffer[0]);
    }

    struct data_args_array_t *write_args_prev = bpf_map_lookup_elem(&active_write_event_args_map, &id);
    if (write_args_prev != NULL)
    {
        bpf_printk("write_args already exists\n");
    }
    
    u64 err = bpf_map_update_elem(&active_write_event_args_map, &id, &write_args, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_enter_write: map update failed (err: %d)\n", err);
    }
    
    return 0;
}

SEC("kretprobe/ksys_write")
int BPF_KRETPROBE(handle_write_ret, ssize_t ret)
{
    u64 bytes_count = (u64)ret;
    if (bytes_count <= 0)
    {
        return 0;
    }
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_array_t *write_args = bpf_map_lookup_elem(&active_write_event_args_map, &id);
    if (write_args == NULL) return 0;
    
    char line_buffer[1];
    bpf_probe_read_kernel(line_buffer, 1, write_args->buf);
    if (!is_resp_connection(line_buffer, 1))
    {
        bpf_printk("exit 0x%02x ", line_buffer[0]);
    }
    process_data_2(id, write_args, bytes_count, false);
    u64 err = bpf_map_delete_elem(&active_write_event_args_map, &id);
    if (err != 0) {
        bpf_printk("sys_exit_write: map delete failed (err: %d)\n", err);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_sendmsg")
int sys_enter_sendmsg(struct trace_event_raw_sys_enter *ctx)
{
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_t write_args = {};
    write_args.fd = (int)BPF_CORE_READ(ctx, args[0]);
    write_args.buf = (char *)BPF_CORE_READ(ctx, args[1]);
    u32 pid = id >> 32;
    u64 pid_fd = ((u64)pid << 32) | (u64)write_args.fd;

    struct conn_info_t *conn_info = bpf_map_lookup_elem(&conn_info_map, &pid_fd);
    if (conn_info == NULL)
    {
        return 0;
    }
    // bpf_printk("sendmsg: %llu\n", pid_fd);
    struct data_args_t *write_args_prev = bpf_map_lookup_elem(&active_write_args_map, &id);
    if (write_args_prev != NULL)
    {
        bpf_printk("write_args already exists\n");
    }
    u64 err = bpf_map_update_elem(&active_write_args_map, &id, &write_args, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_enter_sendmsg: map update failed (err: %d)\n", err);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendmsg")
int sys_exit_sendmsg(struct trace_event_raw_sys_exit *ctx)
{
    u64 bytes_count = (u64)BPF_CORE_READ(ctx, ret);
    if (bytes_count <= 0)
    {
        return 0;
    }
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_t *write_args = bpf_map_lookup_elem(&active_write_args_map, &id);
    if (write_args == NULL) return 0;
    process_data(id, write_args, bytes_count, false);

    u64 err = bpf_map_delete_elem(&active_write_args_map, &id);
    if (err != 0) {
        bpf_printk("sys_exit_sendmsg: map delete failed (err: %d)\n", err);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_sendto")
int sys_enter_sendto(struct trace_event_raw_sys_enter *ctx)
{
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_t write_args = {};
    write_args.fd = (int)BPF_CORE_READ(ctx, args[0]);
    write_args.buf = (char *)BPF_CORE_READ(ctx, args[1]);
    u32 pid = id >> 32;
    u64 pid_fd = ((u64)pid << 32) | (u64)write_args.fd;

    struct conn_info_t *conn_info = bpf_map_lookup_elem(&conn_info_map, &pid_fd);
    if (conn_info == NULL)
    {
        return 0;
    }
    // bpf_printk("sendto: %llu\n", pid_fd);
    struct data_args_t *write_args_prev = bpf_map_lookup_elem(&active_write_args_map, &id);
    if (write_args_prev != NULL)
    {
        bpf_printk("write_args already exists\n");
    }
    u64 err = bpf_map_update_elem(&active_write_args_map, &id, &write_args, BPF_ANY);
    if (err != 0) {
        bpf_printk("sys_enter_sendto: map update failed (err: %d)\n", err);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendto")
int sys_exit_sendto(struct trace_event_raw_sys_exit *ctx)
{
    u64 bytes_count = (u64)BPF_CORE_READ(ctx, ret);
    if (bytes_count <= 0)
    {
        return 0;
    }
    u64 id = bpf_get_current_pid_tgid();
    struct data_args_t *write_args = bpf_map_lookup_elem(&active_write_args_map, &id);
    if (write_args == NULL) return 0;
    process_data(id, write_args, bytes_count, false);

    u64 err = bpf_map_delete_elem(&active_write_args_map, &id);
    if (err != 0) {
        bpf_printk("sys_exit_sendto: map delete failed (err: %d)\n", err);
    }
    return 0;
}

