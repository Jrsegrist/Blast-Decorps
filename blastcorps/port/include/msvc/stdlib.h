/* <stdlib.h> for the ultralib objects built with MSVC (port/CMakeLists.txt):
 * found before ultralib's include/compiler/modern_gcc/stdlib.h.  What
 * ultralib's libc sources use; ldiv and lldiv are ultralib's own (MSVC would
 * treat them as its intrinsics: #pragma function). */
#ifndef _STDLIB_H
#define _STDLIB_H

typedef struct DIV_T {
    int quot;
    int rem;
} div_t;
typedef struct LDIV_T {
    long quot;
    long rem;
} ldiv_t;
typedef struct lldiv_t {
    long long quot;
    long long rem;
} lldiv_t;

#ifndef NULL
#define NULL 0
#endif

div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);
#pragma function(ldiv, lldiv)

#endif
