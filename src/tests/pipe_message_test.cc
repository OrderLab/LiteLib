#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits.h>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "pipe_message_def.hpp"

void round_trip() {
  int fds[2];
  assert(pipe2(fds, O_NONBLOCK) == 0);
  lite::pipe_message_t sent{lite::PipeMessage::kEnterEmergencyMode, "/tmp/redis.sock"};
  assert(sent.write(fds[1]));
  lite::pipe_message_t received;
  assert(received.read(fds[0]));
  assert(received.action == sent.action && received.backend_port == sent.backend_port);
  close(fds[0]);
  close(fds[1]);
}

void old_fragmented_writer() {
  int fds[2];
  assert(pipe2(fds, O_NONBLOCK) == 0);
  auto action = lite::PipeMessage::kEnterEmergencyMode;
  assert(::write(fds[1], &action, sizeof(action)) == sizeof(action));
  lite::pipe_message_t received;
  assert(!received.read(fds[0]));
  assert(errno == EAGAIN);
  close(fds[0]);
  close(fds[1]);
  std::cout << "Confirmed: original fragmented framing can wake reader before length/payload\n";
}

void full_pipe_does_not_publish_partial_frame() {
  int fds[2];
  assert(pipe2(fds, O_NONBLOCK) == 0);
  const int size = fcntl(fds[1], F_GETPIPE_SZ);
  assert(size >= PIPE_BUF * 2);
  std::vector<char> buffer(size, 'x');
  assert(::write(fds[1], buffer.data(), buffer.size()) == size);
  // Free one complete page, then leave fewer bytes than the new frame needs.
  assert(::read(fds[0], buffer.data(), PIPE_BUF) == PIPE_BUF);
  assert(::write(fds[1], buffer.data(), PIPE_BUF - 3) == PIPE_BUF - 3);
  lite::pipe_message_t message{lite::PipeMessage::kEnterEmergencyMode, "/tmp/redis.sock"};
  assert(!message.write(fds[1]));
  assert(errno == EAGAIN);
  size_t drained = 0;
  ssize_t count;
  while ((count = ::read(fds[0], buffer.data(), buffer.size())) > 0) drained += count;
  assert(drained == static_cast<size_t>(size - 3));
  close(fds[0]);
  close(fds[1]);
}

void size_limit() {
  int fds[2];
  assert(pipe2(fds, O_NONBLOCK) == 0);
  lite::pipe_message_t maximum{
      lite::PipeMessage::kEnterEmergencyMode,
      std::string(PIPE_BUF - sizeof(lite::PipeMessage) - sizeof(int), 'a')};
  assert(maximum.write(fds[1]));
  lite::pipe_message_t received;
  assert(received.read(fds[0]) && received.backend_port == maximum.backend_port);
  maximum.backend_port.push_back('b');
  assert(!maximum.write(fds[1]) && errno == EMSGSIZE);
  char byte;
  assert(::read(fds[0], &byte, 1) == -1 && errno == EAGAIN);
  close(fds[0]);
  close(fds[1]);
}

void concurrent_writers() {
  int fds[2];
  assert(pipe2(fds, O_CLOEXEC) == 0);
  assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
  constexpr int messages_per_writer = 1000;
  auto send = [&](int writer) {
    for (int i = 0; i < messages_per_writer; ++i) {
      lite::pipe_message_t message{
          writer == 0 ? lite::PipeMessage::kEnterEmergencyMode
                      : lite::PipeMessage::kExitEmergencyMode,
          std::to_string(writer) + ":" + std::to_string(i)};
      assert(message.write(fds[1]));
    }
  };
  std::thread first(send, 0), second(send, 1);
  int received[2] = {0, 0};
  for (int i = 0; i < 2 * messages_per_writer; ++i) {
    pollfd readable{fds[0], POLLIN, 0};
    assert(poll(&readable, 1, 5000) == 1);
    assert(readable.revents & POLLIN);
    lite::pipe_message_t message;
    assert(message.read(fds[0]));
    const int writer =
        message.action == lite::PipeMessage::kEnterEmergencyMode ? 0 : 1;
    assert(message.backend_port ==
           std::to_string(writer) + ":" + std::to_string(received[writer]));
    ++received[writer];
  }
  first.join();
  second.join();
  assert(received[0] == messages_per_writer && received[1] == messages_per_writer);
  close(fds[0]);
  close(fds[1]);
}

int main(int argc, char **argv) {
  old_fragmented_writer();
  for (int i = 0; i < 100; ++i) round_trip();
  if (argc > 1 && std::string(argv[1]) == "atomic") {
    full_pipe_does_not_publish_partial_frame();
    size_limit();
    concurrent_writers();
  }
  std::cout << "PASS: control-message checks\n";
}
