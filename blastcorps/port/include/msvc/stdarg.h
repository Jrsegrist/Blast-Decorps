/* <stdarg.h> for the ultralib objects built with MSVC (port/CMakeLists.txt):
 * found before ultralib's include/compiler/modern_gcc/stdarg.h, which uses
 * gcc's builtins.  The Windows x64 calling convention's va_list: a char *
 * walking 8-byte argument slots (larger or odd-sized arguments are passed by
 * reference); __va_start is a compiler intrinsic. */
#ifndef _STDARG_H
#define _STDARG_H
typedef char *va_list;
void __cdecl __va_start(va_list *, ...);
#define va_start(ap, v) ((void) __va_start(&(ap), (v)))
#define va_arg(ap, t) \
    ((sizeof(t) > 8 || (sizeof(t) & (sizeof(t) - 1)) != 0) ? **(t **) (((ap) += 8) - 8) : *(t *) (((ap) += 8) - 8))
#define va_end(ap) ((void) ((ap) = (va_list) 0))
#endif
