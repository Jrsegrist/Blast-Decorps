/* Threads, message queues, events, timers and the virtual clock.
 *
 * Model: libultra's single-CPU priority scheduler, run cooperatively.  Every
 * game thread is a host fiber; exactly one runs at a time, and control only
 * changes hands where libultra would switch threads:
 *   - a thread blocks (osRecvMesg on an empty queue, osSendMesg on a full one),
 *   - a thread readies a higher-priority thread (osStartThread, osSendMesg,
 *     osSetThreadPri),
 *   - an "interrupt" (VI retrace, timer, RSP/RDP done) readies a thread that
 *     outranks the running one.
 * Run queues are ordered by priority, FIFO among equals, as in libultra.
 *
 * Time is virtual (count units, 46.875 MHz) and only moves when every game
 * thread is blocked (or a busy-wait calls port_spin): the clock then jumps
 * to the next pending event (retrace, timer, task completion).  Running game
 * code takes no virtual time; task durations (PlatConfig *_cycles) model the
 * RCP.  So a run is a pure function of the ROM, the options and the input.
 *
 * The idle thread (priority 0, `while (1) {}`) is parked when it lowers
 * itself to OS_PRIORITY_IDLE: the host loop plays its part. */
#include "plat.h"

#define MAX_THREADS 16

typedef struct {
    OSThread *t;
    HostFiber fiber;
    void (*entry)(void *);
    void *arg;
    int alive;
} HostThread;

static HostThread g_threads[MAX_THREADS];
static HostFiber g_main_fiber;
static OSThread *g_running;   /* NULL while the host loop runs */
static OSThread * N64P g_runq;    /* ready threads, highest priority first */
static OSThread * N64P g_active;  /* all created threads (tlnext) */
static OSThread *g_idle;      /* parked idle thread */

u64 plat_now;
u32 plat_vi_count;

static void sync_point(void *ra, char kind, void *arg);
static void entry_add(const char *name);

OSThread *plat_running(void) {
    return g_running;
}

static HostThread *host_of(OSThread *t) {
    int i;
    for (i = 0; i < MAX_THREADS; i++)
        if (g_threads[i].t == t) return &g_threads[i];
    return NULL;
}

const char *plat_thread_name(OSThread *t) {
    static char buf[32];
    if (t == NULL) return "host";
    /* "id@addr" */
    {
        char *p = buf;
        u32 id = (u32) t->id, a = (u32) t, i;
        const char *hex = "0123456789ABCDEF";
        if (id >= 10) *p++ = '0' + (id / 10) % 10;
        *p++ = '0' + id % 10;
        *p++ = '@';
        for (i = 0; i < 8; i++) *p++ = hex[(a >> (28 - 4 * i)) & 0xF];
        *p = 0;
    }
    return buf;
}

/* ---- queues ------------------------------------------------------------ */

/* insert behind every thread of the same or higher priority */
static void enqueue(OSThread * N64P *q, OSThread *t) {
    OSThread * N64P *pp = q;
    while (*pp != NULL && (*pp)->priority >= t->priority) pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
    t->queue = q;
}

static OSThread *pop(OSThread * N64P *q) {
    OSThread *t = *q;
    if (t != NULL) {
        *q = t->next;
        t->next = NULL;
        t->queue = NULL;
    }
    return t;
}

static void unlink_thread(OSThread *t) {
    OSThread * N64P *pp = t->queue;
    if (pp == NULL) return;
    while (*pp != NULL && *pp != t) pp = &(*pp)->next;
    if (*pp == t) *pp = t->next;
    t->next = NULL;
    t->queue = NULL;
}

/* ---- switching --------------------------------------------------------- */

/* Give the CPU to the best ready thread (or the host loop).  Called by the
 * running thread after it has put itself where it belongs (a queue, or
 * nowhere if stopped).  Returns when this thread is dispatched again. */
static void dispatch_from_thread(void) {
    OSThread *next = pop(&g_runq);
    plat_stats.thread_switches++;
    if (next == NULL) {
        g_running = NULL;
        host_fiber_switch(g_main_fiber);
    } else {
        HostThread *h = host_of(next);
        g_running = next;
        next->state = OS_STATE_RUNNING;
        host_fiber_switch(h->fiber);
    }
}

static void enqueue_and_yield(OSThread * N64P *q) {
    if (q != NULL) enqueue(q, g_running);
    dispatch_from_thread();
}

/* after readying threads: let a higher-priority one take over */
static void maybe_preempt(void) {
    if (g_running != NULL && g_runq != NULL && g_running->priority < g_runq->priority) {
        g_running->state = OS_STATE_RUNNABLE;
        enqueue_and_yield(&g_runq);
    }
}

static void fiber_main(void *arg) {
    HostThread *h = arg;
    h->entry(h->arg);
    /* libultra's __osCleanupThread: a returning thread destroys itself */
    osDestroyThread(NULL);
}

/* ---- threads ----------------------------------------------------------- */

void osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri) {
    HostThread *h = host_of(t);
    int i;
    (void) sp; /* game stacks stay unused; fibers have host stacks */
    if (h == NULL) {
        for (i = 0; i < MAX_THREADS && g_threads[i].t != NULL; i++) {}
        if (i == MAX_THREADS) host_fatal("osCreateThread: too many threads");
        h = &g_threads[i];
    } else if (h->fiber != NULL) {
        /* re-created (the front end's pak thread): drop the old fiber */
        host_fiber_delete(h->fiber);
    }
    h->t = t;
    h->entry = entry;
    h->arg = arg;
    h->alive = 1;
    h->fiber = host_fiber_create(fiber_main, h);
    t->next = NULL;
    t->priority = pri;
    t->queue = NULL;
    t->tlnext = g_active;
    g_active = t;
    t->state = OS_STATE_STOPPED;
    t->flags = 0;
    t->id = id;
    t->fp = 0;
    if (host_verbose) host_log("thread: create %s pri %d entry %p\n", plat_thread_name(t), (int) pri, entry);
}

void osStartThread(OSThread *t) {
    sync_point(PORT_RETADDR(), 't', t);
    switch (t->state) {
        case OS_STATE_WAITING:
            t->state = OS_STATE_RUNNABLE;
            enqueue(&g_runq, t);
            break;
        case OS_STATE_STOPPED:
            if (t->queue == NULL || t->queue == &g_runq) {
                t->state = OS_STATE_RUNNABLE;
                enqueue(&g_runq, t);
            } else {
                t->state = OS_STATE_WAITING;
                enqueue(t->queue, t);
                {
                    OSThread *w = pop(t->queue);
                    w->state = OS_STATE_RUNNABLE;
                    enqueue(&g_runq, w);
                }
            }
            break;
        default:
            break;
    }
    maybe_preempt();
}

void osDestroyThread(OSThread *t) {
    OSThread * N64P *pp;
    HostThread *h;
    if (t == NULL) t = g_running;
    if (t == NULL) return;
    if (t->state != OS_STATE_STOPPED) unlink_thread(t);
    for (pp = &g_active; *pp != NULL; pp = &(*pp)->tlnext) {
        if (*pp == t) {
            *pp = t->tlnext;
            break;
        }
    }
    t->state = OS_STATE_STOPPED;
    h = host_of(t);
    if (host_verbose) host_log("thread: destroy %s\n", plat_thread_name(t));
    if (t == g_running) {
        if (h) h->alive = 0; /* its fiber is deleted when the slot is reused */
        dispatch_from_thread();
        host_fatal("destroyed thread resumed");
    }
    if (h != NULL) {
        host_fiber_delete(h->fiber);
        h->fiber = NULL;
        h->alive = 0;
    }
}

void osSetThreadPri(OSThread *t, OSPri pri) {
    if (t == NULL) t = g_running;
    if (t == NULL) return;
    if (t == g_running && pri == OS_PRIORITY_IDLE) {
        /* the idle thread parks; the host loop is the idle loop */
        t->priority = pri;
        t->state = OS_STATE_RUNNABLE;
        g_idle = t;
        if (host_verbose) host_log("thread: %s is the idle thread (parked)\n", plat_thread_name(t));
        dispatch_from_thread();
        host_fatal("idle thread resumed");
    }
    if (t->priority != pri) {
        t->priority = pri;
        if (t != g_running && t->state != OS_STATE_STOPPED && t->queue != NULL) {
            OSThread * N64P *q = t->queue;
            unlink_thread(t);
            enqueue(q, t);
        }
        maybe_preempt();
    }
}

OSPri osGetThreadPri(OSThread *t) {
    if (t == NULL) t = g_running;
    return t ? t->priority : 0;
}

void osYieldThread(void) {
    if (g_running == NULL) return;
    g_running->state = OS_STATE_RUNNABLE;
    enqueue_and_yield(&g_runq);
}

/* ---- messages ---------------------------------------------------------- */

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count) {
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

static void block_on(OSThread * N64P *q) {
    if (g_running == NULL) host_fatal("blocking message call outside a game thread");
    g_running->state = OS_STATE_WAITING;
    enqueue_and_yield(q);
}

static void wake_one(OSThread * N64P *q) {
    OSThread *t = pop(q);
    if (t != NULL) {
        t->state = OS_STATE_RUNNABLE;
        enqueue(&g_runq, t);
        maybe_preempt();
    }
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) {
    sync_point(PORT_RETADDR(), 's', mq);
    /* A frame: the main thread hands the scheduler (gScheduler) a task with
     * the frame flag (0x40; 405F0.c rsp_send_task).  Counted (and dumped)
     * here, where the main thread is at the same point on any machine. */
    if ((u32) mq == 0x80315440u && (u32) msg >= 0x80000000u && (u32) msg < 0x80800000u &&
        (*(u32 *) ((u8 *) msg + 0x08) & 0x40))
        plat_on_frame();
    while (mq->validCount >= mq->msgCount) {
        if (flag != OS_MESG_BLOCK) return -1;
        block_on(&mq->fullqueue);
    }
    mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    mq->validCount++;
    wake_one(&mq->mtqueue);
    return 0;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) {
    sync_point(PORT_RETADDR(), 'j', mq);
    while (mq->validCount >= mq->msgCount) {
        if (flag != OS_MESG_BLOCK) return -1;
        block_on(&mq->fullqueue);
    }
    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    wake_one(&mq->mtqueue);
    return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag) {
    sync_point(PORT_RETADDR(), 'r', mq);
    while (mq->validCount == 0) {
        if (flag == OS_MESG_NOBLOCK) return -1;
        block_on(&mq->mtqueue);
    }
    if (msg != NULL) *msg = mq->msg[mq->first];
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    wake_one(&mq->fullqueue);
    return 0;
}

/* ---- events ------------------------------------------------------------ */

static struct {
    OSMesgQueue *mq;
    OSMesg msg;
} g_events[OS_NUM_EVENTS];

void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg) {
    if (e < OS_NUM_EVENTS) {
        g_events[e].mq = mq;
        g_events[e].msg = msg;
    }
}

void plat_post_event(int e) {
    if (e >= 0 && e < OS_NUM_EVENTS && g_events[e].mq != NULL) {
        if (osSendMesg(g_events[e].mq, g_events[e].msg, OS_MESG_NOBLOCK) < 0 && host_verbose)
            host_log("event %d: queue full, dropped\n", e);
    }
}

/* interrupt masks: nothing is asynchronous here, so they only need to
 * round-trip the value */
static OSIntMask g_intmask = OS_IM_ALL;
OSIntMask osSetIntMask(OSIntMask im) {
    OSIntMask old = g_intmask;
    g_intmask = im;
    return old;
}

/* COP0 Status as a thread sees it: CU1, all interrupts unmasked, IE.  The
 * scheduler (get_cop0_status & 0x1000, the pre-NMI line) treats a clear IM4
 * as a reset in progress. */
u32 __osGetSR(void) {
    return 0x2000FF01;
}
void __osSetSR(u32 v) {
    (void) v;
}

/* ---- virtual-time events ------------------------------------------------ */

#define MAX_PEV 64
typedef struct {
    u64 when;
    u64 seq;
    int kind;
    void *p;
} PEvent;
static PEvent g_pev[MAX_PEV];
static int g_npev;
static u64 g_seq;

void plat_event_add(u64 when, int kind, void *p) {
    if (g_npev == MAX_PEV) host_fatal("event queue full");
    g_pev[g_npev].when = when;
    g_pev[g_npev].seq = g_seq++;
    g_pev[g_npev].kind = kind;
    g_pev[g_npev].p = p;
    g_npev++;
}

void plat_event_remove(int kind, void *p) {
    int i;
    for (i = 0; i < g_npev; i++) {
        if (g_pev[i].kind == kind && g_pev[i].p == p) {
            g_pev[i] = g_pev[--g_npev];
            i--;
        }
    }
}

/* earliest event; ties: retrace first, then in the order they were armed */
static int next_event(void) {
    int i, best = -1;
    for (i = 0; i < g_npev; i++) {
        if (best < 0 || g_pev[i].when < g_pev[best].when ||
            (g_pev[i].when == g_pev[best].when &&
             ((g_pev[i].kind == PEV_VI) > (g_pev[best].kind == PEV_VI) ||
              ((g_pev[i].kind == PEV_VI) == (g_pev[best].kind == PEV_VI) && g_pev[i].seq < g_pev[best].seq))))
            best = i;
    }
    return best;
}

static void deliver(PEvent ev) {
    switch (ev.kind) {
        case PEV_VI:
            plat_vi_count++;
            plat_event_add(ev.when + PLAT_VI_PERIOD, PEV_VI, NULL);
            plat_vi_retrace();
            if (plat_cfg.max_vis && plat_vi_count >= plat_cfg.max_vis) {
                host_log("stopping: %u retraces reached\n", (unsigned) plat_vi_count);
                host_exit(0);
            }
            break;
        case PEV_TIMER:
            plat_timer_fire((OSTimer *) ev.p);
            break;
        case PEV_SP_DONE:
            plat_post_event(OS_EVENT_SP);
            break;
        case PEV_DP_DONE:
            plat_post_event(OS_EVENT_DP);
            break;
        case PEV_SI_DONE:
            plat_post_event(OS_EVENT_SI);
            break;
    }
}

int plat_advance(void) {
    int i = next_event();
    PEvent ev;
    if (i < 0) return 0;
    ev = g_pev[i];
    g_pev[i] = g_pev[--g_npev];
    if (ev.when > plat_now) plat_now = ev.when;
    deliver(ev);
    return 1;
}

/* A game busy-wait (PORT_SPIN in the game source): the spinning thread keeps
 * the CPU, so time passes until the next interrupt, which may hand the CPU
 * to a higher-priority thread for a while. */
void port_spin(void) {
    if (!plat_advance()) host_fatal("port_spin: nothing will ever happen");
}

/* Let time pass on the running thread until T: it keeps the CPU, interrupts
 * (and the threads they ready) take their turn, as during a busy-wait. */
void plat_spin_until(u64 t) {
    for (;;) {
        int i = next_event();
        if (i < 0 || g_pev[i].when > t) break;
        plat_advance();
    }
    if (plat_now < t) plat_now = t;
}

/* ---- CPU time from the emulator (--sync FILE) ----------------------------
 * Game code takes no virtual time here, so a thread reaches its next message
 * call earlier than on the N64.  port/tools/compare.py logs, in the emulator,
 * the time of every osSendMesg / osRecvMesg / osJamMesg / osStartThread a
 * game function (func_*, or a renamed one the file lists as "G NAME") makes, per thread: lines "S ID NAME TIME" (thread
 * id, calling function, native count units).  Before the native thread makes
 * the same call, the clock runs up to that time (plat_spin_until), so threads
 * interleave with each other and with retraces as they did in the emulator.
 * A call from another function than the next logged one looks a few entries
 * ahead for it; otherwise it is not synchronised (counted as a mismatch). */
#define SYNC_IDS 16
#define SYNC_AHEAD 64
typedef struct {
    u64 t;
    const char *name;
    u32 q;      /* the queue (or thread) argument */
    s32 gvi;    /* the game's retrace counter gSchedFrameCount there (-1: not logged) */
    char kind;  /* s/r/j/t: osSendMesg, osRecvMesg, osJamMesg, osStartThread */
} SyncEv;
static struct {
    SyncEv *ev;
    u32 n, cap, next;
    u32 used, late, mism;
} g_sync[SYNC_IDS];
static int g_sync_on;
static s32 sync_gvi_pending;
/* game functions renamed from their func_ names (tools_port/names.txt): "G NAME" lines */
static const char **g_sync_game;
static u32 g_sync_ngame, g_sync_capgame;

#define GAME_VI_COUNTER (*(volatile u32 *) 0x803156C4) /* the scheduler's frameCount */

void plat_sync_load(const char *path) {
    unsigned size;
    char *text = host_read_file(path, &size), *p, *end;
    u32 lines = 0;
    if (text == NULL) host_fatal("can't read --sync file %s", path);
    for (p = text, end = text + size; p < end;) {
        char *eol = p, *q, *name;
        u32 id = 0;
        u64 t = 0;
        while (eol < end && *eol != '\n') eol++;
        *eol = 0;
        /* "G NAME": a game function whose name isn't func_XXXXXXXX */
        if (p[0] == 'G' && p[1] == ' ' && p[2]) {
            if (g_sync_ngame == g_sync_capgame) {
                g_sync_capgame = g_sync_capgame ? g_sync_capgame * 2 : 256;
                g_sync_game = host_realloc(g_sync_game, g_sync_capgame * sizeof(*g_sync_game));
            }
            g_sync_game[g_sync_ngame++] = p + 2;
        }
        /* "S ID NAME KIND QUEUE(hex) TIME" */
        if (p[0] == 'S' && p[1] == ' ') {
            q = p + 2;
            while (*q >= '0' && *q <= '9') id = id * 10 + (*q++ - '0');
            if (*q == ' ' && id < SYNC_IDS) {
                char kind;
                u32 qa = 0;
                name = ++q;
                while (*q && *q != ' ') q++;
                if (*q == ' ' && q[1] && q[2] == ' ') {
                    *q++ = 0;
                    kind = *q;
                    q += 2;
                    for (;; q++) {
                        if (*q >= '0' && *q <= '9') qa = qa * 16 + (u32) (*q - '0');
                        else if (*q >= 'A' && *q <= 'F') qa = qa * 16 + (u32) (*q - 'A' + 10);
                        else break;
                    }
                    if (*q == ' ') q++;
                    while (*q >= '0' && *q <= '9') t = t * 10 + (u64) (*q++ - '0');
                    {
                        s32 gv = -1, neg = 0;
                        if (*q == ' ') {
                            q++;
                            if (*q == '-') neg = 1, q++;
                            for (gv = 0; *q >= '0' && *q <= '9'; q++) gv = gv * 10 + (*q - '0');
                            if (neg) gv = -1;
                        }
                        sync_gvi_pending = gv;
                    }
                    if (g_sync[id].n == g_sync[id].cap) {
                        g_sync[id].cap = g_sync[id].cap ? g_sync[id].cap * 2 : 1024;
                        g_sync[id].ev = host_realloc(g_sync[id].ev, g_sync[id].cap * sizeof(SyncEv));
                    }
                    g_sync[id].ev[g_sync[id].n].t = t;
                    g_sync[id].ev[g_sync[id].n].name = name;
                    g_sync[id].ev[g_sync[id].n].kind = kind;
                    g_sync[id].ev[g_sync[id].n].q = qa;
                    g_sync[id].ev[g_sync[id].n].gvi = sync_gvi_pending;
                    g_sync[id].n++;
                    lines++;
                    if (kind == 'e') entry_add(name);
                }
            }
        }
        p = eol + 1;
    }
    g_sync_on = 1;
    if (!plat_cfg.quiet) host_log("sync: %u thread switch points from %s\n", (unsigned) lines, path);
}

static int name_eq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* a game function: func_XXXXXXXX, or a name the sync file lists ("G") */
static int sync_game_function(const char *name) {
    u32 i;
    if (name[0] == 'f' && name[1] == 'u' && name[2] == 'n' && name[3] == 'c' && name[4] == '_') return 1;
    for (i = 0; i < g_sync_ngame; i++)
        if (name_eq(g_sync_game[i], name)) return 1;
    return 0;
}

static void sync_point(void *ra, char kind, void *arg) {
    const char *name;
    u32 id, k, j;
    if (!g_sync_on || g_running == NULL) return;
    name = plat_sym_name((u32) ra, NULL);
    if (name == NULL || !sync_game_function(name))
        return;
    id = (u32) g_running->id;
    if (id >= SYNC_IDS) return;
    /* emulator calls more than a retrace in the past were missed */
    k = g_sync[id].next;
    while (k < g_sync[id].n && g_sync[id].ev[k].t + PLAT_VI_PERIOD < plat_now) k++;
    g_sync[id].next = k;
    if (k >= g_sync[id].n) return;
    for (j = k; j < g_sync[id].n && j < k + SYNC_AHEAD; j++)
        if (g_sync[id].ev[j].kind == kind && g_sync[id].ev[j].q == (u32) arg && name_eq(g_sync[id].ev[j].name, name))
            break;
    if (j == g_sync[id].n || j == k + SYNC_AHEAD) {
        if (host_verbose)
            host_log("sync: vi %u thread %u: %s %c %08X not in the next %u (next: %s %c %08X at vi %u)\n",
                     (unsigned) plat_vi_count, (unsigned) id, name, kind, (unsigned) (u32) arg, SYNC_AHEAD,
                     g_sync[id].ev[k].name, g_sync[id].ev[k].kind, (unsigned) g_sync[id].ev[k].q,
                     (unsigned) (g_sync[id].ev[k].t / PLAT_VI_PERIOD));
        g_sync[id].mism++;
        return;
    }
    if (j != k) {
        /* calls the native thread didn't make: skip them only if they are
         * close in time, else this call is one the emulator didn't make */
        if (g_sync[id].ev[j].t > g_sync[id].ev[k].t + PLAT_VI_PERIOD / 4) {
            if (host_verbose)
                host_log("sync: vi %u thread %u: %s %c %08X is not the next call (%s %c %08X at vi %u); unsynced\n",
                         (unsigned) plat_vi_count, (unsigned) id, name, kind, (unsigned) (u32) arg,
                         g_sync[id].ev[k].name, g_sync[id].ev[k].kind, (unsigned) g_sync[id].ev[k].q,
                         (unsigned) (g_sync[id].ev[k].t / PLAT_VI_PERIOD));
            g_sync[id].mism++;
            return;
        }
        if (host_verbose)
            host_log("sync: vi %u thread %u: skipped %u emulator calls before %s %c %08X (first: %s %c %08X)\n",
                     (unsigned) plat_vi_count, (unsigned) id, (unsigned) (j - k), name, kind, (unsigned) (u32) arg,
                     g_sync[id].ev[k].name, g_sync[id].ev[k].kind, (unsigned) g_sync[id].ev[k].q);
        g_sync[id].mism++;
    }
    g_sync[id].next = j + 1;
    g_sync[id].used++;
    if (host_verbose >= 2)
        host_log("sync: thread %u %s %c %08X at %.3f -> %.3f\n", (unsigned) id, name, kind, (unsigned) (u32) arg,
                 (double) plat_now / PLAT_VI_PERIOD, (double) g_sync[id].ev[j].t / PLAT_VI_PERIOD);
    if (plat_now > g_sync[id].ev[j].t) {
        g_sync[id].late++;
        if (host_verbose && plat_now - g_sync[id].ev[j].t > PLAT_VI_PERIOD / 8)
            host_log("sync: vi %u thread %u: %s %c %08X %.2f retraces late\n", (unsigned) plat_vi_count,
                     (unsigned) id, name, kind, (unsigned) (u32) arg,
                     (double) (plat_now - g_sync[id].ev[j].t) / PLAT_VI_PERIOD);
    } else
        plat_spin_until(g_sync[id].ev[j].t);
    /* the game's retrace counter must read what it read in the emulator here
     * (a retrace close to the switch point can fall either side of it) */
    /* (not for the scheduler, which counts them; at most two retraces) */
    if (g_sync[id].ev[j].gvi >= 0 && id != 5) {
        u32 stop = plat_vi_count + 2;
        while ((s32) (GAME_VI_COUNTER - (u32) g_sync[id].ev[j].gvi) < 0 && plat_vi_count < stop && plat_advance()) {}
    }
}

/* Function-entry switch points ('e'): the entries of the functions --sync
 * lists with kind 'e' (those that read the game's retrace counter)
 * synchronise.  gcc/clang: the game files are built -finstrument-functions,
 * __cyg_profile_func_enter gets the function's address.  MSVC: only the
 * functions listed get a hook, patched into their entry at start-up
 * (host_entry_hook, entry_x64.asm), which runs before any of their
 * instructions; port_entry_hook gets the function's address. */
#define ENTRY_HASH 256
static u32 g_entry[ENTRY_HASH];
static int g_entry_on;

#if defined(_MSC_VER) && !defined(__clang__)
#define PORT_ENTRY_DETOUR 1
/* the function at FN, its entry hooked; 0 if it can't be */
static u32 entry_key(u32 fn) {
    return host_entry_hook(fn) == 0 ? fn : 0;
}
#else
#define entry_key(fn) (fn)
void __cyg_profile_func_enter(void *fn, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *site) __attribute__((no_instrument_function));
#endif

static void entry_add(const char *name) {
    u32 a = plat_sym_addr(name), h;
    if (a == 0) return;
    a = entry_key(a);
    if (a == 0) host_fatal("--sync: can't hook the entry of %s", name);
    for (h = (a >> 4) % ENTRY_HASH; g_entry[h] != 0 && g_entry[h] != a; h = (h + 1) % ENTRY_HASH) {}
    g_entry[h] = a;
    g_entry_on = 1;
}

/* --calls NAME,NAME: log every call of these game functions (with the caller
 * and the frame; port/tools/compare.py calls lines them up with the
 * emulator's) */
#define CALL_HASH 64
static u32 g_call[CALL_HASH];
static int g_call_on;

void plat_calls_init(const char *names) {
    char buf[64];
    while (names && *names) {
        u32 n = 0, a, h;
        while (*names && *names != ',' && n < sizeof buf - 1) buf[n++] = *names++;
        buf[n] = 0;
        if (*names == ',') names++;
        a = plat_sym_addr(buf);
        if (a == 0 || entry_key(a) == 0) {
            host_log("--calls: no function %s (or its entry can't be hooked)\n", buf);
            continue;
        }
        for (h = (a >> 4) % CALL_HASH; g_call[h] != 0 && g_call[h] != a; h = (h + 1) % CALL_HASH) {}
        g_call[h] = a;
        g_call_on = 1;
    }
}

static void entry_sync(u32 a) {
    u32 h;
    for (h = (a >> 4) % ENTRY_HASH; g_entry[h] != 0; h = (h + 1) % ENTRY_HASH)
        if (g_entry[h] == a) {
            sync_point((void *) (size_t) a, 'e', NULL);
            return;
        }
}

#ifdef PORT_ENTRY_DETOUR
/* entry_x64.asm: FN = the hooked game function, RSP its stack pointer at
 * entry (its return address is on top), SAVED its argument registers r9,
 * r8, rdx, rcx (in that order) */
void port_entry_hook(unsigned long long fn, unsigned long long rsp, const unsigned long long *saved) {
    u32 a = (u32) fn, h;
    if (g_call_on) {
        for (h = (a >> 4) % CALL_HASH; g_call[h] != 0; h = (h + 1) % CALL_HASH)
            if (g_call[h] == a) {
                const char *c = plat_sym_name((u32) *(const unsigned long long *) (size_t) rsp, NULL);
                host_log("call: %s from %s frame %u args %x,%x,%x,%x\n", plat_sym_name(a, NULL), c ? c : "?",
                         (unsigned) plat_stats.frames, (unsigned) saved[3], (unsigned) saved[2],
                         (unsigned) saved[1], (unsigned) saved[0]);
                break;
            }
    }
    if (g_entry_on) entry_sync(a);
}
#else
void __cyg_profile_func_enter(void *fn, void *site) {
    u32 a = (u32) fn, h;
    if (g_call_on) {
        for (h = (a >> 4) % CALL_HASH; g_call[h] != 0; h = (h + 1) % CALL_HASH)
            if (g_call[h] == a) {
                const char *c = plat_sym_name((u32) site, NULL);
                /* the first four argument words: just above fn's return
                 * address on the stack (found by value, above this hook's own
                 * arguments).  Only for functions that weren't inlined (an
                 * inlined body reports its container's return address); build
                 * the game files with HL_EXTRA=-fno-inline when it matters. */
                u32 *p = (u32 *) __builtin_frame_address(0) + 4, *arg = NULL;
                int i;
                for (i = 0; i < 1024; i++)
                    if (p[i] == (u32) site) {
                        arg = p + i + 1;
                        break;
                    }
                if (arg)
                    host_log("call: %s from %s frame %u args %x,%x,%x,%x\n", plat_sym_name(a, NULL), c ? c : "?",
                             (unsigned) plat_stats.frames, (unsigned) arg[0], (unsigned) arg[1], (unsigned) arg[2],
                             (unsigned) arg[3]);
                else
                    host_log("call: %s from %s frame %u\n", plat_sym_name(a, NULL), c ? c : "?",
                             (unsigned) plat_stats.frames);
                break;
            }
    }
    if (g_entry_on) entry_sync(a);
}

void __cyg_profile_func_exit(void *fn, void *site) {
    (void) fn;
    (void) site;
}
#endif

void plat_sync_point(void *ra, char kind, void *arg) {
    sync_point(ra, kind, arg);
}

void plat_sync_report(void) {
    u32 i;
    if (!g_sync_on) return;
    for (i = 0; i < SYNC_IDS; i++)
        if (g_sync[i].n)
            host_log("sync: thread %u: %u of %u points used (native already later at %u), %u mismatches\n",
                     (unsigned) i, (unsigned) g_sync[i].used, (unsigned) g_sync[i].n, (unsigned) g_sync[i].late,
                     (unsigned) g_sync[i].mism);
}

/* ---- timers -------------------------------------------------------------- *
 *
 * libultra's timer list as the game's SDK (older than 2.0K, ultralib's src/os/settimer.c, timerintr.c,
 * stoptimer.c) keeps it: one Compare interrupt, a list of timers each holding its countdown relative to the
 * one before it, the first one's relative to __osTimerCounter (the count when the interrupt was last set).
 * This SDK's osSetTimer inserts a timer without first charging the time since __osTimerCounter to the first
 * timer, and re-arms the interrupt only when the new timer goes first, so a timer set while another is
 * pending runs late or early by that time.  It shows when the scheduler's audio timer (6 ms after every
 * other retrace, 2C560.c) and the EEPROM's 12 ms block timers (os_si.c osEepromLongWrite / LongRead) overlap:
 * a block goes out once per retrace, as in mupen64plus, not every 12 ms.  A timer alone in the list (every
 * other case in this game) fires exactly countdown counts after it was set.
 * The list is kept here (host side); the OSTimer in N64 RAM gets the fields osSetTimer sets.  Counts are the
 * port's (following an emulator, plat_emu_counts converts its count values). */
#define MAX_TIMERS 16
static struct {
    OSTimer *t;
    u64 value;   /* counts after the timer before it (the first: after g_timer_counter) */
} g_tlist[MAX_TIMERS];
static int g_ntimers;
static u64 g_timer_counter;   /* __osTimerCounter */

/* __osSetTimerIntr: the Compare interrupt (the one PEV_TIMER event) TIM counts from now */
static void timer_set_intr(u64 tim) {
    g_timer_counter = plat_now;
    plat_event_remove(PEV_TIMER, NULL);
    plat_event_add(plat_now + tim, PEV_TIMER, NULL);
}

/* __osInsertTimer: -> the timer's place in the list; its value then is relative to the one before it */
static int timer_insert(OSTimer *t, u64 value) {
    int i = 0, k;
    u64 tim = value;
    if (g_ntimers == MAX_TIMERS) host_fatal("osSetTimer: too many timers");
    while (i < g_ntimers && tim > g_tlist[i].value) tim -= g_tlist[i++].value;
    if (i < g_ntimers) g_tlist[i].value -= tim;
    for (k = g_ntimers; k > i; k--) g_tlist[k] = g_tlist[k - 1];
    g_tlist[i].t = t;
    g_tlist[i].value = tim;
    g_ntimers++;
    return i;
}

static int timer_find(OSTimer *t) {
    int i;
    for (i = 0; i < g_ntimers; i++)
        if (g_tlist[i].t == t) return i;
    return -1;
}

static void timer_unlink(int i) {
    for (; i + 1 < g_ntimers; i++) g_tlist[i] = g_tlist[i + 1];
    g_ntimers--;
}

int osStopTimer(OSTimer *t) {
    int i = timer_find(t);
    if (i < 0) return -1;
    if (i + 1 < g_ntimers) g_tlist[i + 1].value += g_tlist[i].value;
    timer_unlink(i);
    t->next = t->prev = NULL;
    if (g_ntimers == 0) plat_event_remove(PEV_TIMER, NULL);   /* __osSetCompare(0) */
    return 0;
}

int osSetTimer(OSTimer *t, OSTime countdown, OSTime interval, OSMesgQueue *mq, OSMesg msg) {
    int at;
    /* (the SDK would link a still-pending timer in twice; the game never re-arms one early) */
    if (timer_find(t) >= 0) osStopTimer(t);
    t->next = t->prev = NULL;
    t->interval = interval;
    t->value = countdown != 0 ? countdown : interval;
    t->mq = mq;
    t->msg = msg;
    /* following an emulator (--clock): its count runs at a different rate */
    at = timer_insert(t, plat_emu_counts(t->value));
    if (at == 0) timer_set_intr(g_tlist[0].value);
    return 0;
}

/* the Compare interrupt (PEV_TIMER): __osTimerInterrupt.  The messages go out after the list is done, as from
 * the exception handler (no thread runs in between). */
void plat_timer_fire(OSTimer *unused) {
    OSTimer *sent[MAX_TIMERS];
    int nsent = 0, i;
    (void) unused;
    for (;;) {
        OSTimer *t;
        u64 elapsed;
        if (g_ntimers == 0) {
            g_timer_counter = 0;
            break;
        }
        t = g_tlist[0].t;
        elapsed = plat_now - g_timer_counter;
        g_timer_counter = plat_now;
        if (elapsed < g_tlist[0].value) {
            g_tlist[0].value -= elapsed;
            timer_set_intr(g_tlist[0].value);
            break;
        }
        timer_unlink(0);
        t->value = t->interval;
        sent[nsent++] = t;
        if (t->interval != 0) timer_insert(t, plat_emu_counts(t->interval));
    }
    for (i = 0; i < nsent; i++)
        if (sent[i]->mq != NULL) osSendMesg(sent[i]->mq, sent[i]->msg, OS_MESG_NOBLOCK);
}

/* ---- the host loop ------------------------------------------------------- */

void plat_run(void) {
    g_main_fiber = host_fiber_init();
    /* retraces at fixed multiples of the period from power-on (the clock may
     * start late: --boot-count) */
    plat_vi_count = (u32) (plat_now / PLAT_VI_PERIOD);
    plat_event_add((plat_now / PLAT_VI_PERIOD + 1) * PLAT_VI_PERIOD, PEV_VI, NULL);
    for (;;) {
        OSThread *next = pop(&g_runq);
        if (next != NULL) {
            HostThread *h = host_of(next);
            g_running = next;
            next->state = OS_STATE_RUNNING;
            plat_stats.thread_switches++;
            host_fiber_switch(h->fiber);
            /* back here: every game thread is blocked (or parked) */
            continue;
        }
        if (!plat_advance()) host_fatal("deadlock: no thread ready and no event pending");
    }
}
