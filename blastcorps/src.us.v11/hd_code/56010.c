#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* Empty, the same code as debug_printf (homes a0-a3 and returns); nothing calls it (0x8029A7D0). */
void debug_printf_unused(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
}

/* The debug printf (asserts and messages), compiled out: an empty variadic
 * function (IDO homes a0-a3 as for any varargs function). The native port
 * can print the messages (bc_headless.exe --print) (0x8029A7E4). */
void debug_printf(const char *fmt, ...) {
#if defined(NON_MATCHING) && defined(PORT_HOST)
    va_list ap; /* <stdarg.h> from game/port.h */

    va_start(ap, fmt);
    port_game_print(fmt, ap);
    va_end(ap);
#endif
}
