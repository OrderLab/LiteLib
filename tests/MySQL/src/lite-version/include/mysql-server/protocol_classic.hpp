#pragma once

#include "mysql-server/com_data.hpp"
#include "mysql-server/my_command.hpp"
#include "mysql-server/utils.hpp"

bool get_command_and_parse_packet(COM_DATA *data, enum_server_command *cmd,
                                  uchar *raw_packet, size_t packet_length);