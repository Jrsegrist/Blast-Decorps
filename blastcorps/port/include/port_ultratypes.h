/* Forced-included before every game file in the port build: the libultra
 * scalar types with fixed widths on the host (the SDK header uses `long` for
 * 32-bit types, which is 32 bits on Windows too, but this keeps the 32-bit
 * and any later 64-bit build identical).  Defines the SDK header's include
 * guard so <PR/ultratypes.h> is skipped. */
#ifndef _ULTRATYPES_H_
#define _ULTRATYPES_H_
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef short s16;
typedef int s32;
typedef long long s64;
typedef volatile unsigned char vu8;
typedef volatile unsigned short vu16;
typedef volatile unsigned int vu32;
typedef volatile unsigned long long vu64;
typedef volatile signed char vs8;
typedef volatile short vs16;
typedef volatile int vs32;
typedef volatile long long vs64;
typedef float f32;
typedef double f64;
#ifndef _SIZE_T
#define _SIZE_T
#define _SIZE_T_DEF
#if defined(_MSC_VER) && !defined(__clang__) && defined(_WIN64)
typedef unsigned __int64 size_t;
#elif defined(_MSC_VER) && !defined(__clang__)
typedef unsigned int size_t;
#else
typedef __SIZE_TYPE__ size_t;
#endif
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif
#include "port_n64ptr.h"
#if defined(_MSC_VER) && !defined(__clang__)
/* MSVC: calls to sinf/cosf go to the SDK's (ultralib's gu, N64-identical
 * results), never to the compiler's intrinsic forms (as gcc's
 * -fno-builtin-sinf); os_hw.c defines them */
float sinf(float);
float cosf(float);
#pragma function(sinf, cosf)
#endif
#endif
