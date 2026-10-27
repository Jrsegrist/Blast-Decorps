/* Host (Windows) services: fibers, logging, files, crash reporting. */
#include <windows.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>
#include "plat_host.h"
#include "audio/port_audio.h"

int host_verbose;
int host_gui;
void *host_gui_window;
const char *host_log_path;
/* the per-frame trace: its own buffer and a file handle (not stdio), so the
 * crash reporter can write what is buffered without the C runtime's locks
 * and without knowing a C runtime's FILE internals */
static HANDLE g_trace = INVALID_HANDLE_VALUE;
static char g_trace_buf[1 << 16];
static unsigned g_trace_len;

/* UTF-8 -> UTF-16 (0 if the text isn't valid UTF-8 or doesn't fit) */
static int to_wide(const char *s, wchar_t *w, int n) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
}

void *host_fopen(const char *path, const char *mode) {
    static wchar_t wp[4096];
    wchar_t wm[16];
    if (to_wide(path, wp, 4096) > 0 && to_wide(mode, wm, 16) > 0) return _wfopen(wp, wm);
    return fopen(path, mode);
}

HostProf host_prof;

double host_ms(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double) now.QuadPart * 1000.0 / (double) freq.QuadPart;
}

void host_message(const char *text, int error) {
    static wchar_t w[8192];
    fprintf(stderr, "message box (%s)%s: %s\n", error ? "error" : "warning",
            host_no_msgbox ? ", not shown (--no-msgbox)" : "", text);
    fflush(stderr);
    if (host_no_msgbox) return;   /* --no-msgbox / BC_NO_MSGBOX (crash.c): automated runs */
    if (host_gui_window != NULL) ShowWindow((HWND) host_gui_window, SW_HIDE);
    if (to_wide(text, w, 8192) <= 0) MultiByteToWideChar(CP_ACP, 0, text, -1, w, 8192);
    MessageBoxW(NULL, w, L"Blast Corps",
                MB_OK | MB_SETFOREGROUND | MB_TOPMOST | (error ? MB_ICONERROR : MB_ICONWARNING));
}

/* Fiber stacks: reserve 1 MB each (commit grows on demand). */
#define FIBER_STACK_COMMIT 0x10000
#define FIBER_STACK_RESERVE 0x100000

typedef struct {
    void (*fn)(void *);
    void *arg;
} FiberStart;

static void WINAPI fiber_trampoline(void *p) {
    FiberStart s = *(FiberStart *) p;
    free(p);
    /* game threads store addresses of their locals in N64 memory (4 bytes) */
    if ((ULONG_PTR) &s >> 32) host_fatal("a fiber stack is above 4 GB (%p)", (void *) &s);
    host_set_fpu_mode();
    s.fn(s.arg);
    host_fatal("fiber function returned");
}

HostFiber host_fiber_init(void) {
    void *f = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
    if (f == NULL) host_fatal("ConvertThreadToFiberEx failed (%lu)", GetLastError());
    return f;
}

HostFiber host_fiber_create(void (*fn)(void *), void *arg) {
    FiberStart *s = malloc(sizeof *s);
    void *f;
    s->fn = fn;
    s->arg = arg;
    f = CreateFiberEx(FIBER_STACK_COMMIT, FIBER_STACK_RESERVE, FIBER_FLAG_FLOAT_SWITCH, fiber_trampoline, s);
    if (f == NULL) host_fatal("CreateFiberEx failed (%lu)", GetLastError());
    return f;
}

void host_fiber_switch(HostFiber f) {
    if (f != GetCurrentFiber()) SwitchToFiber(f);
}

void host_fiber_delete(HostFiber f) {
    if (f != NULL && f != GetCurrentFiber()) DeleteFiber(f);
}

void host_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* is environment variable NAME set (debug switches) */
int host_env(const char *name) {
    return getenv(name) != NULL;
}

static void trace_flush(void) {
    DWORD w;
    if (g_trace != INVALID_HANDLE_VALUE && g_trace_len) WriteFile(g_trace, g_trace_buf, g_trace_len, &w, NULL);
    g_trace_len = 0;
}

void host_fatal(const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    trace_flush();
    fprintf(stderr, "FATAL: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    if (host_gui) {
        static char msg[4096];
        va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        host_message(msg, 1);
    }
    /* no DLL/atexit teardown: the error may have left RT64's threads or the
     * heap in any state (stdio is flushed; saves are written as they happen) */
    TerminateProcess(GetCurrentProcess(), 2);
    ExitProcess(2);
}

int host_trace_open(const char *path) {
    static wchar_t wp[4096];
    if (to_wide(path, wp, 4096) > 0)
        g_trace = CreateFileW(wp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    else
        g_trace = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return g_trace != INVALID_HANDLE_VALUE ? 0 : -1;
}

/* text as stdio's "w" mode writes it: \n -> \r\n */
void host_trace(const char *fmt, ...) {
    char line[1024];
    va_list ap;
    int n, i;
    if (g_trace == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int) sizeof line) n = sizeof line - 1;
    if (g_trace_len + 2 * (unsigned) n > sizeof g_trace_buf) trace_flush();
    for (i = 0; i < n; i++) {
        if (line[i] == '\n') g_trace_buf[g_trace_len++] = '\r';
        g_trace_buf[g_trace_len++] = line[i];
    }
}

void *host_read_file(const char *path, unsigned *size) {
    FILE *f = host_fopen(path, "rb");
    long n;
    void *p;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = malloc(n > 0 ? n : 1);
    if (n > 0 && fread(p, 1, n, f) != (size_t) n) {
        fclose(f);
        free(p);
        return NULL;
    }
    fclose(f);
    if (size) *size = (unsigned) n;
    return p;
}

int host_write_file(const char *path, const void *data, unsigned size) {
    FILE *f = host_fopen(path, "wb");
    if (f == NULL) return -1;
    if (fwrite(data, 1, size, f) != size) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}

void *host_realloc(void *p, unsigned size) {
    void *q = realloc(p, size);
    if (q == NULL) host_fatal("out of memory");
    return q;
}

#if defined(_MSC_VER) && defined(_WIN64)
/* ---- function-entry hooks (MSVC: os_thread.c's --sync entry points, --calls) ----
 * MSVC's x64 code starts every function with an instruction of at least 2
 * bytes (hot-patchable), and the exe is linked /FUNCTIONPADMIN (free bytes
 * before every function).  A hooked function's first instruction becomes `jmp short` to
 * the padding, which jumps to a thunk of its own (entry_x64.asm explains
 * it); the thunk runs the relocated first instruction and jumps back. */
#define PAD 8   /* /FUNCTIONPADMIN:8 (CMakeLists.txt) */

/* the length of the x86-64 instruction at P (0 if it isn't one this knows or
 * can't be moved: a short branch); *REL = the offset of its 32-bit
 * PC-relative field (a RIP-relative operand, a rel32 call/jump/jcc) or -1 */
static int insn_length(const unsigned char *p, int *rel) {
    int i = 0, rex_w = 0, osz = 0, modrm = 0, imm = 0, op;
    *rel = -1;
    for (;;) {
        unsigned char b = p[i];
        if (b == 0x66) osz = 1;
        else if (b != 0x67 && b != 0xF0 && b != 0xF2 && b != 0xF3 && b != 0x2E && b != 0x3E && b != 0x26 &&
                 b != 0x36 && b != 0x64 && b != 0x65)
            break;
        if (++i > 4) return 0;
    }
    if ((p[i] & 0xF0) == 0x40) rex_w = (p[i++] >> 3) & 1;
    op = p[i++];
    if (op == 0x0F) {
        int op2 = p[i++];
        if (op2 == 0x38) i++, modrm = 1;
        else if (op2 == 0x3A) i++, modrm = 1, imm = 1;
        else if (op2 >= 0x80 && op2 <= 0x8F) *rel = i, imm = 4;   /* jcc rel32 */
        else if (op2 == 0x05 || op2 == 0x0B || op2 == 0x31 || op2 == 0xA2 || op2 == 0x77 || op2 == 0xA0 ||
                 op2 == 0xA1 || op2 == 0xA8 || op2 == 0xA9 || (op2 >= 0xC8 && op2 <= 0xCF))
            ;
        else {
            modrm = 1;
            if ((op2 >= 0x70 && op2 <= 0x73) || op2 == 0xA4 || op2 == 0xAC || op2 == 0xBA || op2 == 0xC2 ||
                (op2 >= 0xC4 && op2 <= 0xC6))
                imm = 1;
        }
    } else if (op < 0x40) {
        switch (op & 7) {
            case 0: case 1: case 2: case 3: modrm = 1; break;
            case 4: imm = 1; break;
            case 5: imm = osz ? 2 : 4; break;
            default: return 0;
        }
    } else if (op >= 0x50 && op <= 0x5F) ;
    else if (op == 0x63 || (op >= 0x84 && op <= 0x8F) || (op >= 0xD0 && op <= 0xD3) || (op >= 0xD8 && op <= 0xDF) ||
             op == 0xFE || op == 0xFF)
        modrm = 1;
    else if (op == 0x68) imm = 4;
    else if (op == 0x6A || op == 0xA8 || (op >= 0xB0 && op <= 0xB7) || op == 0xCD) imm = 1;
    else if (op == 0x69 || op == 0x81 || op == 0xC7) modrm = 1, imm = osz ? 2 : 4;
    else if (op == 0x6B || op == 0x80 || op == 0x83 || op == 0xC0 || op == 0xC1 || op == 0xC6) modrm = 1, imm = 1;
    else if ((op >= 0x90 && op <= 0x99) || (op >= 0x9B && op <= 0x9F) || (op >= 0xA4 && op <= 0xA7) ||
             (op >= 0xAA && op <= 0xAF) || op == 0xC3 || op == 0xC9 || op == 0xCC || (op >= 0xF5 && op <= 0xFD))
        ;
    else if (op >= 0xA0 && op <= 0xA3) imm = 8;
    else if (op == 0xA9) imm = osz ? 2 : 4;
    else if (op >= 0xB8 && op <= 0xBF) imm = rex_w ? 8 : osz ? 2 : 4;
    else if (op == 0xC2) imm = 2;
    else if (op == 0xC8) imm = 3;
    else if (op == 0xE8 || op == 0xE9) *rel = i, imm = 4;
    else if (op == 0xF6 || op == 0xF7) {
        modrm = 1;
        if (((p[i] >> 3) & 7) < 2) imm = op == 0xF6 ? 1 : osz ? 2 : 4;
    } else
        return 0;   /* (short branches, VEX, ...) */
    if (modrm) {
        int m = p[i++], mod = m >> 6, rm = m & 7;
        if (mod != 3) {
            if (rm == 4) {
                if (mod == 0 && (p[i] & 7) == 5) i += 4;
                i++;
            } else if (mod == 0 && rm == 5) {
                *rel = i;
                i += 4;
            }
            i += mod == 1 ? 1 : mod == 2 ? 4 : 0;
        }
    }
    return i + imm;
}

void port_entry_common(void);   /* entry_x64.asm */
static unsigned char *g_thunks;
static unsigned g_thunks_used;
#define THUNK_SIZE 48
#define THUNK_AREA 0x10000

int host_entry_hook(unsigned fn) {
    unsigned char *f = (unsigned char *) (ULONG_PTR) fn, *t;
    LONG_PTR d;
    int len, rel, i;
    INT32 v;
    DWORD old;
    if (f[0] == 0xEB && f[1] == (unsigned char) -(PAD + 2)) return 0;   /* hooked already */
    len = insn_length(f, &rel);
    if (len < 2 || len > 15) return -1;
    for (i = 1; i <= PAD; i++)
        if (f[-i] != 0xCC && f[-i] != 0x90 && f[-i] != 0x00) return -1;   /* not padding */
    if (g_thunks == NULL) {
        /* within 2 GB of the exe and of N64 memory: right after the image */
        ULONG_PTR base = (ULONG_PTR) GetModuleHandleW(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *) (base + ((IMAGE_DOS_HEADER *) base)->e_lfanew);
        ULONG_PTR a = (base + nt->OptionalHeader.SizeOfImage + 0xFFFF) & ~(ULONG_PTR) 0xFFFF;
        for (i = 0; i < 4096 && g_thunks == NULL; i++, a += 0x10000)
            g_thunks = VirtualAlloc((void *) a, THUNK_AREA, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (g_thunks == NULL) return -1;
    }
    if (g_thunks_used + THUNK_SIZE > THUNK_AREA) return -1;
    t = g_thunks + g_thunks_used;
    g_thunks_used += THUNK_SIZE;
    /* push FN (an imm32, sign-extended: the exe is below 2 GB); call port_entry_common */
    t[0] = 0x68;
    memcpy(t + 1, &fn, 4);
    t[5] = 0xE8;
    v = (INT32) ((unsigned char *) port_entry_common - (t + 10));
    memcpy(t + 6, &v, 4);
    /* the first instruction, its PC-relative field moved with it */
    memcpy(t + 10, f, len);
    if (rel >= 0) {
        memcpy(&v, f + rel, 4);
        d = (LONG_PTR) v + (f - (t + 10));
        if (d != (INT32) d) return -1;
        v = (INT32) d;
        memcpy(t + 10 + rel, &v, 4);
    }
    /* jmp FN + len */
    t[10 + len] = 0xE9;
    v = (INT32) ((f + len) - (t + 15 + len));
    memcpy(t + 11 + len, &v, 4);
    /* the padding before FN: jmp THUNK; FN: jmp short to the padding */
    if (!VirtualProtect(f - PAD, PAD + 2, PAGE_EXECUTE_READWRITE, &old)) return -1;
    f[-PAD] = 0xE9;
    v = (INT32) (t - (f - PAD + 5));
    memcpy(f - PAD + 1, &v, 4);
    f[1] = (unsigned char) -(PAD + 2);
    f[0] = 0xEB;
    VirtualProtect(f - PAD, PAD + 2, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f - PAD, PAD + 2);
    return 0;
}
#else
int host_entry_hook(unsigned fn) {
    (void) fn;
    return -1;
}
#endif

/* ---- host memory RT64 reads through its extended RDRAM addresses (plat_host.h) ----
 * Between RDRAM's end and 4 GB.  On the PC with a big graphics card the
 * driver had filled that range by the time the first ticker screen came up
 * (the widescreen ticker then asked for memory there every frame, and logged
 * each failure), so it is taken once, first thing at start-up. */
#define LOW_START 0x80800000ull   /* RDRAM's end (0x80000000 + 8 MB) */
#define LOW_END 0x100000000ull
#define LOW_SIZE 0x100000u        /* 1 MB (the ticker takes 256 KB) */
#define LOW_HOLE 0xE7300000ull    /* --low-test 1: the 1 MB left free */
unsigned host_low_test;
static unsigned char *g_low;
static unsigned g_low_used, g_low_fails, g_low_tried, g_low_crowded;
static const char *g_low_how = "";

/* exactly at a, or NULL */
static void *low_at(unsigned long long a, unsigned size) {
    void *p = VirtualAlloc((void *) (ULONG_PTR) a, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p != NULL && (unsigned long long) (ULONG_PTR) p != a) {
        VirtualFree(p, 0, MEM_RELEASE);
        p = NULL;
    }
    return p;
}

/* `size` bytes in [LOW_START, LOW_END): a few chosen addresses, else the
 * first free range that fits (64 KB granularity) */
static unsigned char *low_find(unsigned size) {
    static const unsigned long long pref[] = {0x81000000ull, 0x90000000ull, 0xA0000000ull, 0xC0000000ull};
    unsigned long long a;
    MEMORY_BASIC_INFORMATION m;
    unsigned i;
    void *p;
    for (i = 0; i < sizeof pref / sizeof pref[0]; i++)
        if ((p = low_at(pref[i], size)) != NULL) {
            g_low_how = "a chosen address";
            return p;
        }
    for (a = LOW_START; a < LOW_END && VirtualQuery((void *) (ULONG_PTR) a, &m, sizeof m) == sizeof m;) {
        unsigned long long end = (unsigned long long) (ULONG_PTR) m.BaseAddress + m.RegionSize;
        if (m.State == MEM_FREE) {
            unsigned long long s = (a + 0xFFFF) & ~0xFFFFull;
            if (end > LOW_END) end = LOW_END;
            if (s + size <= end && (p = low_at(s, size)) != NULL) {
                g_low_how = "a free range found by a search";
                return p;
            }
        }
        if (end <= a) break;
        a = end;
    }
    return NULL;
}

/* (testing) reserve every free 1 MB in [LOW_START, LOW_END), but `hole` */
static void low_crowd(unsigned long long hole) {
    unsigned long long a;
    MEMORY_BASIC_INFORMATION m;
    for (a = LOW_START; a < LOW_END; a += 0x100000ull) {
        if (a == hole) continue;
        if (VirtualQuery((void *) (ULONG_PTR) a, &m, sizeof m) == sizeof m && m.State == MEM_FREE &&
            VirtualAlloc((void *) (ULONG_PTR) a, 0x100000, MEM_RESERVE, PAGE_NOACCESS) != NULL)
            g_low_crowded++;
    }
}

int host_low_reserve(void) {
    if (host_low_test & 1) low_crowd(LOW_HOLE);
    if (!(host_low_test & 4) && g_low == NULL) {
        g_low_tried = 1;
        g_low = low_find(LOW_SIZE);
    }
    if (host_low_test & 2) low_crowd(~0ull);
    if (host_low_test)
        host_log("low memory test %u: %u MB of [0x80800000, 4 GB) reserved by the test%s\n", host_low_test,
                 g_low_crowded, (host_low_test & 4) ? "; no reservation at start-up" : "");
    return g_low != NULL ? 0 : -1;
}

void *host_low_alloc(unsigned size, const char *who) {
    unsigned char *p;
    size = (size + 15) & ~15u;
    if (g_low == NULL && !g_low_tried) {   /* not reserved at start-up (--low-test 4): now */
        g_low_tried = 1;
        g_low = low_find(LOW_SIZE);
        if (g_low != NULL) g_low_how = "at its first use";
    }
    if (g_low == NULL || size > LOW_SIZE - g_low_used) {
        if (g_low_fails++ == 0)
            host_log("live: no host memory below 4 GB for %s (%u bytes; %s): logged once\n", who, size,
                     g_low == NULL ? "none could be reserved" : "the reservation is full");
        return NULL;
    }
    p = g_low + g_low_used;
    g_low_used += size;
    return p;
}

void host_low_summary(void) {
    if (g_low != NULL)
        host_log("live: low memory: %u KB at 0x%08llX (%s%s), %u KB used, %u requests refused\n", LOW_SIZE / 1024,
                 (unsigned long long) (ULONG_PTR) g_low, g_low_how,
                 g_low_tried && !(host_low_test & 4) ? ", at start-up" : "", g_low_used / 1024, g_low_fails);
    else if (g_low_tried)
        host_log("live: low memory: none below 4 GB could be reserved; %u requests refused\n", g_low_fails);
}

void host_set_fpu_mode(void) {
    /* MXCSR: FTZ (bit 15) like the VR4300's FPCSR FS; all exceptions masked
     * (default).  Rounding: nearest (default), as FPCSR RM_RN. */
    _mm_setcsr(_mm_getcsr() | 0x8000);
}

void (*host_exit_hook)(void);

void host_exit(int code) {
    void (*hook)(void) = host_exit_hook;
    host_exit_hook = NULL;
    if (hook != NULL) hook();
    port_audio_close();
    trace_flush();
    if (g_trace != INVALID_HANDLE_VALUE) CloseHandle(g_trace);
    g_trace = INVALID_HANDLE_VALUE;
    fflush(stdout);
    fflush(stderr);
    ExitProcess((UINT) code);
}

/* Debugging (--watch FRAME:ADDR[:N], plat_core.c): from frame FRAME, log the
 * first N (default 20) writes to ADDR's 4 KB page: eip and the stack's code
 * pointers (symbolise with bc_headless.syms).  The page is made read-only;
 * each write is let through by single-stepping it and re-protecting. */
static ULONG_PTR g_watch_page;
static DWORD g_watch_left;
static int g_watch_step;
extern unsigned plat_frames(void);

static unsigned g_wf, g_wa, g_wn, g_won;
static LONG WINAPI watch_handler(EXCEPTION_POINTERS *ep);

/* --watch FRAME:ADDR[:N] (WSL doesn't pass this environment to the exe); frame 0: from boot (headless_main.c
 * arms it once the ROM is loaded, before the game's first instruction) */
int host_watch_set(const char *s) {
    if (sscanf(s, "%u:%x:%u", &g_wf, &g_wa, &g_wn) < 2) return -1;
    fprintf(stderr, "watch: page of %08X from frame %u\n", g_wa, g_wf);
    AddVectoredExceptionHandler(1, watch_handler);
    g_won = 1;
    return 0;
}

void host_watch_frame(unsigned frame) {
    unsigned wa = g_wa, wn = g_wn;
    DWORD old;
    if (!g_won || frame != g_wf || g_watch_page) return;
    g_watch_page = wa & ~0xFFFu;
    g_watch_left = wn ? wn : 20;
    VirtualProtect((void *) g_watch_page, 0x1000, PAGE_READONLY, &old);
}

static int watch_filter(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    DWORD old;
    if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && g_watch_step) {
        g_watch_step = 0;
        if (g_watch_left) VirtualProtect((void *) g_watch_page, 0x1000, PAGE_READONLY, &old);
        return 1;
    }
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2 &&
        er->ExceptionInformation[0] == 1 && (er->ExceptionInformation[1] & ~(ULONG_PTR) 0xFFF) == g_watch_page &&
        g_watch_page) {
        /* the stack's words that point into the exe's image (code pointers) */
        ULONG_PTR *sp, lo = (ULONG_PTR) GetModuleHandleW(NULL), hi;
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *) lo;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *) (lo + dos->e_lfanew);
        int i, k = 0;
        hi = lo + nt->OptionalHeader.SizeOfImage;
        sp = (ULONG_PTR *) c->Rsp;
        fprintf(stderr, "WATCH: frame %u write %08lX rip=%08llX stack", plat_frames(),
                (unsigned long) er->ExceptionInformation[1], (unsigned long long) c->Rip);
        {
            /* not past the stack's top: near the top of a fiber's stack, 64 words
             * reach beyond its allocation (the watch then faulted itself) */
            ULONG_PTR top = (ULONG_PTR) ((NT_TIB *) NtCurrentTeb())->StackBase;
            for (i = 0; i < 64 && k < 8 && (ULONG_PTR) &sp[i + 1] <= top; i++)
                if (sp[i] >= lo + 0x1000 && sp[i] < hi) fprintf(stderr, " %08llX", (unsigned long long) sp[i]), k++;
        }
        fprintf(stderr, "\n");
        g_watch_left--;
        VirtualProtect((void *) g_watch_page, 0x1000, PAGE_READWRITE, &old);
        c->EFlags |= 0x100;   /* trap after the store, then re-protect */
        g_watch_step = 1;
        return 1;
    }
    return 0;
}

/* first chance, before any handler: only --watch's own faults and steps
 * (everything else goes on to the program's handlers, then crash.c) */
static LONG WINAPI watch_handler(EXCEPTION_POINTERS *ep) {
    if (g_watch_page && watch_filter(ep)) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

/* crash.c: what the per-frame trace file holds in its buffer, written
 * without the C runtime's lock (the crashed thread may hold it) */
void host_trace_flush_raw(void) {
    trace_flush();   /* (no C runtime involved) */
}
