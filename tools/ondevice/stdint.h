#pragma once
typedef signed char int8_t; typedef unsigned char uint8_t;
typedef short int16_t; typedef unsigned short uint16_t;
typedef int int32_t; typedef unsigned int uint32_t;
#ifdef _WIN32  /* LLP64: long is 32-bit; must match TinyCC's own intptr_t (__PTRDIFF_TYPE__) */
typedef long long int64_t; typedef unsigned long long uint64_t;
typedef long long intptr_t; typedef unsigned long long uintptr_t;
#else  /* LP64 (Android arm64): long is 64-bit */
typedef long int64_t; typedef unsigned long uint64_t;
typedef long intptr_t; typedef unsigned long uintptr_t;
#endif
typedef long long intmax_t; typedef unsigned long long uintmax_t;
#define INT32_MAX 2147483647
#define INT32_MIN (-INT32_MAX-1)
#define UINT32_MAX 4294967295u
#define INT64_MAX 9223372036854775807LL
#define INT64_MIN (-INT64_MAX-1)
#define UINT64_MAX 18446744073709551615ULL
#define INT16_MAX 32767
#define INT16_MIN (-32768)
#define UINT16_MAX 65535
#define INT8_MAX 127
#define INT8_MIN (-128)
#define UINT8_MAX 255
#define SIZE_MAX UINT64_MAX
#define UINT32_C(x) x##u
#define UINT64_C(x) x##ULL
#define INT64_C(x) x##LL
