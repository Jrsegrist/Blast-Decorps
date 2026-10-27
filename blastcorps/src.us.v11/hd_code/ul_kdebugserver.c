/* libultra src/debug/kdebugserver.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here, and
 * ships the debug server in the final ROM. Its kdebugserver is an older
 * design: a small state machine fed one 3-byte rdb packet at a time
 * (state / numChars / numCharsToReceive), with memory and register dump
 * commands, rdb packets of type 2 in the old header layout (2-bit type in
 * the top bits), and __osRdbWriteOK in place of __osRdb_IP6_Empty.
 * Written from ultralib's version and the ROM. */
#include "PR/os_internal.h"
#include "PR/rcp.h"
#define rdbPacket __rdbPacket_ultralib
#include "PR/rdb.h"
#undef rdbPacket

/* rdbPacket as this SDK has it (see ul_syncputchars.c) */
typedef struct {
    unsigned type : 2;
    unsigned : 4;
    unsigned length : 2;
    char buf[3];
} rdbPacket;

#define KDEBUG_TYPE 2

extern OSThread* __osRunningThread;

static s32 state = 0;
static s32 numChars = 0;
static s32 numCharsToReceive = 0;

static u8 buffer[256];
OSThread __osThreadSave;

static void u32_to_string(u32 i, u8* s) {
    s[0] = (i >> 0x18) & 0xFF;
    s[1] = (i >> 0x10) & 0xFF;
    s[2] = (i >> 0x8) & 0xFF;
    s[3] = i & 0xFF;
}

static u32 string_to_u32(u8* s) {
    u32 k;

    k = ((s[0] & 0xFF) << 0x18);
    k |= ((s[1] & 0xFF) << 0x10);
    k |= ((s[2] & 0xFF) << 0x8);
    k |= (s[3] & 0xFF);

    return k;
}

static void send_packet(u8* s, s32 n) {
    rdbPacket packet;
    s32 i;

    packet.type = KDEBUG_TYPE;
    packet.length = n;

    for (i = 0; i < n; i++) {
        packet.buf[i] = s[i];
    }
    *(vu32*)RDB_BASE_REG = *(u32*)&packet;

    while (!(__osGetCause() & CAUSE_IP6)) {
        ;
    }
    *(vu32*)RDB_READ_INTR_REG = 0;
}

static void send(u8* s, s32 n) {
    s32 i;
    s32 end;
    s32 rem;

    if (!__osRdbWriteOK) {
        while (!(__osGetCause() & CAUSE_IP6)) {
            ;
        }
        *(vu32*)RDB_READ_INTR_REG = 0;
        __osRdbWriteOK = 1;
    }

    i = 0;
    rem = n % 3;
    end = n - rem;

    for (; i < end; i += 3) {
        send_packet(&s[i], 3);
    }
    if (rem > 0) {
        send_packet(&s[end], rem);
    }
}

static void process_command_memory(void) {
    u32 addr;
    u32 length;

    addr = string_to_u32(&buffer[1]);
    length = string_to_u32(&buffer[5]);
    send((u8*)addr, length);
}

static void process_command_register(void) {
    send((u8*)&__osThreadSave.context, sizeof(__OSThreadContext));
}

void kdebugserver(u32 a) {
    u32 i;
    rdbPacket packet;

    *(u32*)&packet = a;

    for (i = 0; i < packet.length; i++) {
        buffer[numChars] = packet.buf[i];
        numChars++;
    }
    numCharsToReceive -= packet.length;

    switch (state) {
        case 0:
            switch (packet.buf[0]) {
                case 1:
                    state = 1;
                    numCharsToReceive = 9 - packet.length;
                    break;
                case 2:
                    process_command_register();
                    state = 0;
                    numChars = 0;
                    numCharsToReceive = 0;
                    break;
                default:
                    state = 0;
                    numChars = 0;
                    numCharsToReceive = 0;
                    break;
            }
            break;
        case 1:
            if (numCharsToReceive <= 0) {
                if (buffer[0] == 1) {
                    process_command_memory();
                    state = 0;
                    numChars = 0;
                    numCharsToReceive = 0;
                } else {
                    state = 0;
                    numChars = 0;
                    numCharsToReceive = 0;
                }
            }
            break;
        default:
            state = 0;
            numChars = 0;
            numCharsToReceive = 0;
            break;
    }
}
