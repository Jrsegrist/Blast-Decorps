/* The signed-overflow census build's sanitizer runtime (make -C port
 * overflow-census, tools/overflow_census.py): clang's
 * -fsanitize=signed-integer-overflow -fsanitize-minimal-runtime calls these
 * on every overflowing signed add, subtract, multiply, negate or divide.
 * Each new call site (the handler's return address, in the function with the
 * operation) is appended to ubsan_sites.txt in the current folder; the game
 * goes on (with the wrapped result, as the N64). */
#include <windows.h>
#include <stdio.h>

#define MAXS 8192
static void *g_seen[MAXS];
static unsigned g_n;
static HANDLE g_f = INVALID_HANDLE_VALUE;

static void hit(void *pc, const char *kind) {
    unsigned i;
    char line[64];
    DWORD w;
    int n;
    for (i = 0; i < g_n; i++)
        if (g_seen[i] == pc) return;
    if (g_n < MAXS) g_seen[g_n++] = pc;
    if (g_f == INVALID_HANDLE_VALUE)
        g_f = CreateFileA("ubsan_sites.txt", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    n = snprintf(line, sizeof line, "%016llx %s\n", (unsigned long long) (ULONG_PTR) pc, kind);
    WriteFile(g_f, line, (DWORD) n, &w, NULL);
}

#define H(name) \
    void __ubsan_handle_##name##_minimal(void) { hit(__builtin_return_address(0), #name); } \
    void __ubsan_handle_##name##_minimal_abort(void) { hit(__builtin_return_address(0), #name); }
H(add_overflow)
H(sub_overflow)
H(mul_overflow)
H(negate_overflow)
H(divrem_overflow)
