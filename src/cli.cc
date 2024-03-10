#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>

#include "pipe_message_def.hpp"

void PrintHelp() {
  std::cout << "Usage: LiteClient [-t target] [-m message] [-h help]\n";
  std::cout << "Options:\n";
  std::cout << "  -t, --target      Target named pipe. Default: /tmp/lite\n";
  std::cout << "  -m, --message     Message to send. Default: 0\n";
  std::cout << "  -h, --help        Show this help message.\n";
  std::cout << "Example:\n";
  std::cout << "  LiteClient -t /tmp/lite -m 1\n";
  std::cout << "  LiteClient --target=/tmp/lite --message=1\n\n";

  std::cout << "MessageType:\n";
  constexpr auto values = magic_enum::enum_values<lite::PipeMessage>();
  for (const auto v : values) {
    std::cout << "  " << std::setw(4) << magic_enum::enum_integer(v) << ": " << magic_enum::enum_name(v) << std::endl;
  }
  std::cout << std::endl;
}

int main(int argc, char* argv[]) {
  std::string default_target = "/tmp/lite";
  const char* target = default_target.c_str();
  lite::pipe_message_t message = 0;

  const char* const short_opts = "t:m:h";
  const option long_opts[] = {{"target", required_argument, nullptr, 't'},
                              {"message", required_argument, nullptr, 's'},
                              {"help", no_argument, nullptr, 'h'},
                              {0, 0, 0, 0}};
  int opt;
  while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
         -1) {
    switch (opt) {
      case 't':
        target = optarg;
        break;
      case 'm':
        message = std::stoll(optarg);
        if (magic_enum::enum_cast<lite::PipeMessage>(message).has_value())
          break;
      case 'h':
      default:
        PrintHelp();
        return 0;
    }
  }

  // Open the named pipe
  int fd = open(target, O_WRONLY);
  if (fd == -1) {
    throw std::runtime_error("failed to open the named pipe");
  }

  // Write the message to the named pipe
  ssize_t bytes_written = write(fd, &message, sizeof(message));
  if (bytes_written == -1) {
    throw std::runtime_error("failed to write to the named pipe");
  }

  // Close the named pipe
  if (close(fd) == -1) {
    throw std::runtime_error("failed to close the named pipe");
  }

  std::cout << "Written " << message << " to " << target << std::endl;

  return 0;
}