#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>

void PrintHelp() {
  // std::cout << "Usage: LiteClient [-t target] [-p backend port] [-m message] "
  //              "[-h help]\n";
  // std::cout << "Options:\n";
  // std::cout << "  -t, --target      Target Unix socket. Default: /tmp/lite\n";
  // std::cout << "  -p, --port        Backend port. Default: 60000\n";
  // std::cout << "  -m, --message     Message to send. Default: 0 "
  //              "(kExitEmergencyMode)\n";
  // std::cout << "  -h, --help        Show this help message.\n";
  // std::cout << "Example:\n";
  // std::cout << "  LiteClient -t /tmp/lite -p 60000 -m 1\n";
  // std::cout << "  LiteClient --target=/tmp/lite --port 60000 --message=1\n\n";

  // std::cout << "MessageType:\n";
  // constexpr auto values = magic_enum::enum_values<lite::PipeMessage>();
  // for (const auto v : values) {
  //   std::cout << "  " << std::setw(4) << (int)magic_enum::enum_integer(v)
  //             << ": " << magic_enum::enum_name(v) << std::endl;
  // }
  // std::cout << std::endl;
}

int main(int argc, char* argv[]) {
  // std::string default_target = "/tmp/lite";
  // const char* target = default_target.c_str();
  // lite::pipe_message_t message = {
  //     .action = lite::PipeMessage::kExitEmergencyMode, .backend_port = "60000"};

  // const char* const short_opts = "t:p:m:h";
  // const option long_opts[] = {{"target", required_argument, nullptr, 't'},
  //                             {"port", required_argument, nullptr, 'p'},
  //                             {"message", required_argument, nullptr, 's'},
  //                             {"help", no_argument, nullptr, 'h'},
  //                             {0, 0, 0, 0}};
  // int opt;
  // while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) !=
  //        -1) {
  //   switch (opt) {
  //     case 't':
  //       target = optarg;
  //       break;
  //     case 'p':
  //       message.backend_port = optarg;
  //       break;
  //     case 'm': {
  //       const auto action_opt =
  //           magic_enum::enum_cast<lite::PipeMessage>(std::stoll(optarg));
  //       if (action_opt.has_value()) {
  //         message.action = action_opt.value();
  //         break;
  //       }
  //     }
  //     case 'h':
  //     default:
  //       PrintHelp();
  //       return 0;
  //   }
  // }

  // // Create and connect to Unix domain socket
  // int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  // if (sock_fd == -1) {
  //   throw std::runtime_error("failed to create socket");
  // }

  // struct sockaddr_un addr;
  // memset(&addr, 0, sizeof(addr));
  // addr.sun_family = AF_UNIX;
  // strncpy(addr.sun_path, target, sizeof(addr.sun_path) - 1);

  // if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
  //   close(sock_fd);
  //   throw std::runtime_error("failed to connect to the socket");
  // }

  // // Write the message to the socket
  // if (!message.write(sock_fd)) {
  //   close(sock_fd);
  //   throw std::runtime_error("failed to write to the socket");
  // }

  // // Close the socket
  // if (close(sock_fd) == -1) {
  //   throw std::runtime_error("failed to close the socket");
  // }

  // std::cout << "Written "
  //           << magic_enum::enum_name<lite::PipeMessage>(message.action) << ' '
  //           << message.backend_port << " to " << target << std::endl;

  return 0;
}