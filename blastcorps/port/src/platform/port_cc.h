/* Compiler portability for the port's platform and host code (gcc, clang, MSVC).
 *
 * The platform files that see the game's <ultra64.h> are built without the C
 * runtime's headers (-nostdinc, MSVC /X), so the memory functions are declared
 * here; MSVC expands them inline (#pragma intrinsic) as gcc's builtins do.
 *
 *   PORT_RETADDR()           this function's return address
 *   PORT_NORETURN            before a declaration: the function doesn't return
 *   PORT_NOINLINE            before a definition: never inlined
 *   PORT_PRINTF(f, a)        after a declarator: printf-style format check
 *   PORT_BSWAP16/32/64(x)    byte swaps
 *   PORT_MEMCPY/MEMSET/MEMMOVE/MEMCMP
 */
#ifndef PORT_CC_H
#define PORT_CC_H

#if defined(_MSC_VER) && !defined(__clang__)
#ifndef __cplusplus   /* (C++ files get these from the C runtime's headers) */
void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)
#if !defined(_INC_STDLIB)
unsigned short __cdecl _byteswap_ushort(unsigned short);
unsigned long __cdecl _byteswap_ulong(unsigned long);
unsigned __int64 __cdecl _byteswap_uint64(unsigned __int64);
#endif
#pragma intrinsic(_byteswap_ushort, _byteswap_ulong, _byteswap_uint64)
#if !defined(_INC_STRING)   /* (host files include this after <string.h>) */
void *__cdecl memcpy(void *, const void *, size_t);
void *__cdecl memset(void *, int, size_t);
void *__cdecl memmove(void *, const void *, size_t);
int __cdecl memcmp(const void *, const void *, size_t);
#endif
#pragma intrinsic(memcpy, memset, memcmp)
#endif
#define PORT_RETADDR() _ReturnAddress()
#define PORT_NORETURN __declspec(noreturn)
#define PORT_NOINLINE __declspec(noinline)
#define PORT_PRINTF(f, a)
#define PORT_BSWAP16(x) _byteswap_ushort(x)
#define PORT_BSWAP32(x) ((unsigned int) _byteswap_ulong(x))
#define PORT_BSWAP64(x) _byteswap_uint64(x)
#define PORT_MEMCPY memcpy
#define PORT_MEMSET memset
#define PORT_MEMMOVE memmove
#define PORT_MEMCMP memcmp
#else
#define PORT_RETADDR() __builtin_return_address(0)
#define PORT_NORETURN __attribute__((noreturn))
#define PORT_NOINLINE __attribute__((noinline))
#define PORT_PRINTF(f, a) __attribute__((format(printf, f, a)))
#define PORT_BSWAP16(x) __builtin_bswap16(x)
#define PORT_BSWAP32(x) __builtin_bswap32(x)
#define PORT_BSWAP64(x) __builtin_bswap64(x)
#define PORT_MEMCPY __builtin_memcpy
#define PORT_MEMSET __builtin_memset
#define PORT_MEMMOVE __builtin_memmove
#define PORT_MEMCMP __builtin_memcmp
#endif

#endif
