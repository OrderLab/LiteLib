#include "network_utils.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/un.h>

#include <cstring>
#include <iostream>

#define GLOG_USE_GLOG_EXPORT
#include <glog/logging.h>

namespace lite {

namespace network {
evutil_socket_t TryConnectBackend(const std::string& addr,
                                  const std::string& port) {
  // LOG(INFO) << "Try to connect to backend\n";
  if (addr.empty()) {  // unix socket
    evutil_socket_t backend_fd;
    struct sockaddr_un unix_addr;
    memset(&unix_addr, 0, sizeof(unix_addr));
    unix_addr.sun_family = AF_UNIX;
    strncpy(unix_addr.sun_path, port.c_str(), sizeof(unix_addr.sun_path) - 1);

    int flags = 1;
    struct linger ling = {0, 0};

    backend_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (backend_fd == -1) {
      // PLOG(ERROR) << "failed to connect backend";
      return -1;
    }

    if (setsockopt(backend_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&flags,
                   sizeof(flags)) != 0) {
      PLOG(ERROR) << "failed to set SO_REUSEADDR for backend";
      return -1;
    }

    if (setsockopt(backend_fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&flags,
                   sizeof(flags)) != 0) {
      PLOG(ERROR) << "failed to set SO_KEEPALIVE for backend";
      return -1;
    }

    if (setsockopt(backend_fd, SOL_SOCKET, SO_LINGER, (void*)&ling,
                   sizeof(ling)) != 0) {
      PLOG(ERROR) << "failed to set SO_LINGER for backend";
      return -1;
    }

    // set non-blocking
    if ((flags = fcntl(backend_fd, F_GETFL)) == -1) {
      PLOG(ERROR) << "failed to get flags for backend";
      return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(backend_fd, F_SETFL, flags) == -1) {
      PLOG(ERROR) << "failed to set backend to non-blocking";
      return -1;
    }

    if (connect(backend_fd, (struct sockaddr*)&unix_addr, sizeof(unix_addr)) ==
        -1) {
      /* If the socket is non-blocking, it is ok for connect() to
       * return an EINPROGRESS error here. */
      if (errno != EINPROGRESS) {
        // PLOG(ERROR) << "failed to connect backend";
        return -1;
      }
    }

    // LOG(INFO) << "Backend connected, fd: " << backend_fd << std::endl;
    return backend_fd;
  } else {  // tcp
    evutil_socket_t backend_fd;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(addr.c_str(), port.c_str(), &hints, &res) != 0) {
      PLOG(ERROR) << "getaddrinfo";
      return -1;
    }

    bool connected = false;
    int flags = 1;
    struct linger ling = {0, 0};
    for (; res; res = res->ai_next) {
      backend_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (backend_fd == -1) {
        // PLOG(ERROR) << "failed to connect backend";
        // goto connect_backend_exit;
        continue;
      }

      if (setsockopt(backend_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&flags,
                     sizeof(flags)) != 0) {
        PLOG(ERROR) << "failed to set SO_REUSEADDR for backend";
        continue;
      }

      if (setsockopt(backend_fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&flags,
                     sizeof(flags)) != 0) {
        PLOG(ERROR) << "failed to set SO_KEEPALIVE for backend";
        continue;
      }

      if (setsockopt(backend_fd, SOL_SOCKET, SO_LINGER, (void*)&ling,
                     sizeof(ling)) != 0) {
        PLOG(ERROR) << "failed to set SO_LINGER for backend";
        continue;
      }

      if (setsockopt(backend_fd, IPPROTO_TCP, TCP_NODELAY, (void*)&flags,
                     sizeof(flags)) != 0) {
        PLOG(ERROR) << "failed to set TCP_NODELAY for backend";
        continue;
      }

      // set non-blocking
      int flags;
      if ((flags = fcntl(backend_fd, F_GETFL)) == -1) {
        PLOG(ERROR) << "failed to get flags for backend";
        continue;
      }
      flags |= O_NONBLOCK;
      if (fcntl(backend_fd, F_SETFL, flags) == -1) {
        PLOG(ERROR) << "failed to set backend to non-blocking";
        continue;
      }

      if (connect(backend_fd, res->ai_addr, res->ai_addrlen) == -1) {
        /* If the socket is non-blocking, it is ok for connect() to
         * return an EINPROGRESS error here. */
        if (errno != EINPROGRESS) {
          // PLOG(ERROR) << "failed to connect backend";
          // goto connect_backend_exit;
          continue;
        }
      }

      connected = true;
      break;
    }

    if (!connected) {
      PLOG(ERROR) << "failed to connect backend";
      goto connect_backend_exit;
    }

    // LOG(INFO) << "Backend connected, fd: " << backend_fd << std::endl;

    freeaddrinfo(res);
    return backend_fd;

  connect_backend_exit:
    freeaddrinfo(res);
    return -1;
  }
}

bool Write(const evutil_socket_t fd, const uint8_t buffer[], size_t len) {
  const uint8_t* begin = buffer;
  while (len) {
    ssize_t bytes_written = write(fd, begin, len);
    if (bytes_written <= 0 && errno != EAGAIN) {
      PLOG(ERROR) << "write to " << fd;  // TODO: max tries
      return false;
    } else if (errno == EAGAIN) {
      PLOG(WARNING) << "write to " << fd;
    } else {
      len -= bytes_written;
      begin += bytes_written;
    }
  }
  return true;
}

bool Write(const evutil_socket_t fd, const std::vector<uint8_t> buffer,
           size_t len) {
  return Write(fd, buffer.data(), len);
}

bool Write(const evutil_socket_t fd, const std::vector<uint8_t>&& buffer) {
  return Write(fd, buffer.data(), buffer.size());
}

bool Write(const evutil_socket_t fd,
           const std::unique_ptr<std::vector<uint8_t>>&& buffer) {
  return Write(fd, buffer->data(), buffer->size());
}

bool Write(const evutil_socket_t fd,
           const std::shared_ptr<std::vector<uint8_t>> buffer) {
  return Write(fd, buffer->data(), buffer->size());
}

bool Write(const evutil_socket_t fd,
           const ShmSharedPtr<ShmVector<uint8_t>> buffer) {
  return Write(fd, buffer->data(), buffer->size());
}

TCPID GetTCPID(const evutil_socket_t fd) {
  struct sockaddr_in local_addr;
  socklen_t local_len = sizeof(local_addr);
  if (getsockname(fd, (struct sockaddr*)&local_addr, &local_len) < 0) {
    PLOG(ERROR) << "Failed to get local address";
  }

  struct sockaddr_in peer_addr;
  socklen_t peer_len = sizeof(peer_addr);
  if (getpeername(fd, (struct sockaddr*)&peer_addr, &peer_len) < 0) {
    PLOG(ERROR) << "Failed to get peer address";
  }

  TCPID ret;
  ret.src_ip = ntohl(local_addr.sin_addr.s_addr);
  ret.src_port = ntohs(local_addr.sin_port);
  ret.dst_ip = ntohl(peer_addr.sin_addr.s_addr);
  ret.dst_port = ntohs(peer_addr.sin_port);

  return ret;
}

std::pair<std::vector<int>, std::array<int, 2>> ReceiveSockets(
    const evutil_socket_t fd) {
  static constexpr size_t kMaxFds = 1024;

  std::array<int, 2> lens;
  std::vector<char> cmsgBuf(CMSG_SPACE(sizeof(int) * kMaxFds));

  struct iovec iov;
  iov.iov_base = lens.data();
  iov.iov_len = sizeof(lens);

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgBuf.data();
  msg.msg_controllen = cmsgBuf.size();

  ssize_t received = recvmsg(fd, &msg, 0);
  if (received < 0) {
    LOG(ERROR) << "Error receiving socket message";
    return {std::vector<int>(), lens};
  }

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_level != SOL_SOCKET ||
      cmsg->cmsg_type != SCM_RIGHTS) {
    LOG(ERROR) << "Invalid control message";
    return {std::vector<int>(), lens};
  }

  size_t num_fds = lens[1];
  if (num_fds > kMaxFds) {
    LOG(ERROR) << "Too many FDs received";
    return {std::vector<int>(), lens};
  }

  std::vector<int> received_fds(num_fds);
  memcpy(received_fds.data(), CMSG_DATA(cmsg), sizeof(int) * num_fds);

  return {received_fds, lens};
}

bool SendSockets(const evutil_socket_t fd, std::vector<int>& fds,
                 std::array<int, 2>& lens) {
  size_t totalBytes = sizeof(int) * fds.size();
  std::vector<char> cmsgBuf(CMSG_SPACE(totalBytes), 0);

  struct iovec iov;
  iov.iov_base = lens.data();
  iov.iov_len = sizeof(int) * lens.size();

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgBuf.data();
  msg.msg_controllen = cmsgBuf.size();

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len = CMSG_LEN(totalBytes);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  memcpy(CMSG_DATA(cmsg), fds.data(), totalBytes);

  if (sendmsg(fd, &msg, 0) < 0) {
    LOG(ERROR) << "Failed to transfer sockets to the full process";
    return false;
  }

  return true;
}

int CopyAndReplaceSocket(int fd, int dummy_fd) {
  int new_fd = dup(fd);
  if (new_fd == -1) {
    PLOG(ERROR) << "Failed to duplicate socket " << fd;
    return -1;
  }
  if (dup2(dummy_fd, fd) != fd) {
    PLOG(ERROR) << "Failed to hijack socket " << fd;
    close(new_fd);
    return -1;
  }
  return new_fd;
}

}  // namespace network

}  // namespace lite