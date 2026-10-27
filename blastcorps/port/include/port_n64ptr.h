/* N64 pointers in the port build (port/README.md, "64-bit").
 *
 * N64 memory keeps its N64 layout in every build: a pointer stored in it
 * (a struct field, a pinned global, a table element) is 4 bytes.  The game
 * and SDK headers mark such pointers; in the 64-bit build the marks give
 * 4-byte pointers, everywhere else they expand to nothing (the IDO builds
 * get empty versions from include/n64ptr.h; the port's copies of ultralib's
 * headers carry the marks: tools/ulhdr64.py, data/ultralib_n64ptr.txt).
 *
 *   T * N64P x            an N64 pointer: `__ptr32 __uptr` (4 bytes, zero-
 *                         extended when loaded) on x86_64 (clang with
 *                         -fms-extensions, MSVC); N64PTR(T) is the same type
 *                         for casts: `*(N64PTR(u8) *) p`
 *   N64FN(T) x            a function pointer field of type T (a pointer-to-
 *                         function typedef): a u32 on x86_64 (clang can't use
 *                         __ptr32 function pointers), set with N64FN_SET(f, fn)
 *                         and read with N64FN_GET(T, f); code addresses are in
 *                         the exe image, below 4 GB
 *   N64_IPTR(x)           a signed 32-bit integer that holds an address, about
 *                         to be cast to a pointer: zero-extended on x86_64
 *                         (`(u8 *) N64_IPTR(x)`), not sign-extended
 *   N64_DPTR(x)           a pointer in a static initialiser of N64 data: 0 on
 *                         x86_64, where the start-up pointer table (gensyms.py
 *                         ptrtab) writes the NON_MATCHING ELF's value there
 *   N64_A32               the type of a 32-bit int that holds an N64 address
 *                         added to a pointer (`m += (N64_A32) buf`): s32
 *                         elsewhere, u32 on x86_64 (pointer + s32 sign-extends
 *                         an address >= 0x80000000)
 */
#ifndef PORT_N64PTR_H
#define PORT_N64PTR_H
#if defined(__x86_64__) || defined(_M_X64)
#define PORT_N64PTR_64 1
#define N64P __ptr32 __uptr
#define N64FN(T) unsigned int
#define N64FN_SET(f, fn) ((f) = (unsigned int) (unsigned long long) (fn))
#define N64FN_GET(T, f) ((T) (unsigned long long) (f))
#define N64_IPTR(x) ((unsigned int) (x))
#define N64_DPTR(x) 0
#define N64_A32 unsigned int
#else
#define N64P
#define N64FN(T) T
#define N64FN_SET(f, fn) ((f) = (fn))
#define N64FN_GET(T, f) (f)
#define N64_IPTR(x) (x)
#define N64_DPTR(x) x
#define N64_A32 int
#endif
#define N64PTR(T) T * N64P
/*   N64_KEEP            on a pinned static the compiler could fold away (one
 *                       that is never really written): it stays an object, so
 *                       its initial value is copied into N64 memory */
#if defined(__GNUC__)
#define N64_KEEP __attribute__((used))
#else
#define N64_KEEP
#endif
/* The bit-fields of the marked headers (data/ultralib_n64ptr.txt) that MSVC
 * lays out by Microsoft's rules (a bit-field whose type has another size, or
 * a plain member after a bit-field, starts a new storage unit): written with
 * one storage type there, they get IDO's and gcc's layout (Gpopmtx, Gsetcolor,
 * rdbPacket; layout_check.c checks every packet type). */
#if defined(_MSC_VER) && !defined(__clang__)
#define PORT_BF_UINT unsigned int
#define PORT_BF_BYTE(f) unsigned int f : 8
#define PORT_BF_RDB unsigned char
#else
#define PORT_BF_UINT unsigned char
#define PORT_BF_BYTE(f) unsigned char f
#define PORT_BF_RDB unsigned
#endif
#endif
