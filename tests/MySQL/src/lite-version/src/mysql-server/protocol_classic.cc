#include "mysql-server/protocol_classic.hpp"

#include <cstring>

char *strend(const char *s) {
  while (*s++);
  return (char *)(s - 1);
}

bool get_command_and_parse_packet(COM_DATA *data, enum_server_command *cmd,
                                  uchar *raw_packet, size_t packet_length) {
  /*
    'packet_length' contains length of data, as it was stored in packet
    header. In case of malformed header, my_net_read returns zero.
    If packet_length is not zero, my_net_read ensures that the returned
    number of bytes was actually read from network.
    There is also an extra safety measure in my_net_read:
    it sets packet[packet_length]= 0, but only for non-zero packets.
  */
  if (packet_length == 0) /* safety */
  {
    /* Initialize with COM_SLEEP packet */
    raw_packet[0] = (uchar)COM_SLEEP;
    packet_length = 1;
  }
  /* Do not rely on my_net_read, extra safety against programming errors. */
  raw_packet[packet_length] = '\0'; /* safety */

  if (raw_packet[0] >= COM_END) {
    *cmd = COM_END;  // Wrong command
    goto malformed;
  }

  *cmd = (enum enum_server_command)raw_packet[0];

  // Skip 'command'
  packet_length--;
  raw_packet++;

  switch (*cmd) {
    case COM_INIT_DB: {
      data->com_init_db.db_name = reinterpret_cast<const char *>(raw_packet);
      data->com_init_db.length = packet_length;
      break;
    }
    case COM_REFRESH: {
      if (packet_length < 1) goto malformed;
      data->com_refresh.options = raw_packet[0];
      break;
    }
    case COM_SHUTDOWN: {
      data->com_shutdown.level =
          packet_length == 0 ? SHUTDOWN_DEFAULT
                             : (enum mysql_enum_shutdown_level)raw_packet[0];
      break;
    }
    case COM_PROCESS_KILL: {
      if (packet_length < 4) goto malformed;
      data->com_kill.id = (ulong)uint4korr(raw_packet);
      break;
    }
    case COM_SET_OPTION: {
      if (packet_length < 2) goto malformed;
      data->com_set_option.opt_command = uint2korr(raw_packet);
      break;
    }
    case COM_STMT_EXECUTE: {
      if (packet_length < 9) goto malformed;
      data->com_stmt_execute.stmt_id = uint4korr(raw_packet);
      data->com_stmt_execute.flags = (ulong)raw_packet[4];
      /* stmt_id + 5 bytes of flags */
      /*
        FIXME: params have to be parsed into an array/structure
        by protocol too
      */
      data->com_stmt_execute.params = raw_packet + 9;
      data->com_stmt_execute.params_length = packet_length - 9;
      break;
    }
    case COM_STMT_FETCH: {
      if (packet_length < 8) goto malformed;
      data->com_stmt_fetch.stmt_id = uint4korr(raw_packet);
      data->com_stmt_fetch.num_rows = uint4korr(raw_packet + 4);
      break;
    }
    case COM_STMT_SEND_LONG_DATA: {
#ifndef EMBEDDED_LIBRARY
      if (packet_length < MYSQL_LONG_DATA_HEADER) goto malformed;
#endif
      data->com_stmt_send_long_data.stmt_id = uint4korr(raw_packet);
      data->com_stmt_send_long_data.param_number = uint2korr(raw_packet + 4);
      data->com_stmt_send_long_data.longdata = raw_packet + 6;
      data->com_stmt_send_long_data.length = packet_length - 6;
      break;
    }
    case COM_STMT_PREPARE: {
      data->com_stmt_prepare.query = reinterpret_cast<const char *>(raw_packet);
      data->com_stmt_prepare.length = packet_length;
      break;
    }
    case COM_STMT_CLOSE: {
      if (packet_length < 4) goto malformed;

      data->com_stmt_close.stmt_id = uint4korr(raw_packet);
      break;
    }
    case COM_STMT_RESET: {
      if (packet_length < 4) goto malformed;

      data->com_stmt_reset.stmt_id = uint4korr(raw_packet);
      break;
    }
    case COM_QUERY: {
      data->com_query.query = reinterpret_cast<const char *>(raw_packet);
      data->com_query.length = packet_length;
      break;
    }
    case COM_FIELD_LIST: {
      /*
        We have name + wildcard in packet, separated by endzero
      */
      data->com_field_list.table_name = raw_packet;
      uint len = data->com_field_list.table_name_length =
          strend((char *)raw_packet) - (char *)raw_packet;
      if (len >= packet_length || len > NAME_LEN) goto malformed;
      data->com_field_list.query = raw_packet + len + 1;
      data->com_field_list.query_length = packet_length - len;
      break;
    }
    default:
      break;
  }

  return true;

malformed:
  return false;
}
