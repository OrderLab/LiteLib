#pragma once

#include "mysql-server/com_data.hpp"
#include "mysql-server/my_command.hpp"
#include "mysql-server/utils.hpp"

#ifndef __has_attribute
# define __has_attribute(x) 0
#endif

#if __has_attribute(no_sanitize_undefined)
# define SUPPRESS_UBSAN __attribute__((no_sanitize_undefined))
#else
# define SUPPRESS_UBSAN
#endif

static inline uint16 uint2korr(const uchar *A) SUPPRESS_UBSAN;
static inline uint16 uint2korr(const uchar *A) { return *((uint16 *)A); }
static inline uint32 uint4korr(const uchar *A) SUPPRESS_UBSAN;
static inline uint32 uint4korr(const uchar *A) { return *((uint32 *)A); }
static inline ulonglong uint8korr(const uchar *A) SUPPRESS_UBSAN;
static inline ulonglong uint8korr(const uchar *A) { return *((ulonglong *)A); }

bool get_command_and_parse_packet(COM_DATA *data, enum_server_command *cmd,
                                  uchar *raw_packet, size_t packet_length);