#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef char my_bool;
typedef unsigned char uchar;
typedef unsigned long ulong;
typedef unsigned int uint;
typedef long long int longlong;
typedef unsigned long long int ulonglong;

#define MY_ALIGN(A, L) (((A) + (L) - 1) & ~((L) - 1))
#define ALIGN_SIZE(A) MY_ALIGN((A), sizeof(double))

#define TABLE_COUNTER_TYPE size_t
#define MYSQL_LONG_DATA_HEADER 6
#define SYSTEM_CHARSET_MBMAXLEN 3
#define NAME_CHAR_LEN 64 /* Field/table name length */
#define NAME_LEN (NAME_CHAR_LEN * SYSTEM_CHARSET_MBMAXLEN)