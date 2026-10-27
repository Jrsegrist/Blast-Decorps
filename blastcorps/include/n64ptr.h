/* The N64 pointer marks (N64P, N64FN, ...; port/include/port_n64ptr.h has
 * what each one means) as nothing, for the N64 builds (IDO) and the 32-bit
 * host tools.  The port build defines them first (port_n64ptr.h is forced in),
 * so this changes nothing there. */
#ifndef N64PTR_H
#define N64PTR_H
#ifndef N64P
#define N64P
#define N64PTR(T) T *
#define N64FN(T) T
#define N64FN_SET(f, fn) ((f) = (fn))
#define N64FN_GET(T, f) (f)
#define N64_IPTR(x) (x)
#define N64_DPTR(x) x
#define N64_KEEP
#define N64_A32 s32
#endif
#endif
