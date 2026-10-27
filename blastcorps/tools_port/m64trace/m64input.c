/* m64input: a minimal mupen64plus input plugin (API 2.x) whose controller
 * state is set by the front end that loaded it, e.g. from Python:
 *     lib = ctypes.CDLL("m64input.so")
 *     ctypes.c_uint32.in_dll(lib, "m64input_keys").value = value
 * `value` is the plugin BUTTONS word for controller 1: low 16 bits = the N64
 * button word byte-swapped (plugin bit 0 = R_JPAD = N64 0x0100, ..., bit 7 =
 * A = 0x8000; bit 8 = R_CBUTTONS = 0x0001, ..., bit 13 = L_TRIG = 0x0020),
 * bits 16-23 = stick x (s8), bits 24-31 = stick y (s8).
 * Only controller 1 is plugged, with no accessory or (m64input_plugin = 2)
 * a Controller Pak. Build:
 *     gcc -shared -fPIC -O2 -o m64input.so m64input.c
 * Written for this project from the public mupen64plus plugin API; no
 * mupen64plus headers needed. */
#include <stdint.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))

typedef void *m64p_dynlib_handle;
typedef void (*debugcb)(void *, int, const char *);

/* the CONTROL struct's first three ints; later core versions append a
 * fourth (Type), so only element 0's first fields are written */
typedef struct {
    int Present;
    int RawData;
    int Plugin;
} CONTROL3;

/* mupen64plus API 2.x: only the controller array (not the old zilmar spec) */
typedef struct {
    CONTROL3 *Controls;
} CONTROL_INFO;

EXPORT volatile uint32_t m64input_keys = 0;
EXPORT volatile uint32_t m64input_polls = 0; /* GetKeys calls (controller 1) */
/* controller 1's accessory, set before the plugin is attached: 1 = none,
 * 2 = Controller Pak (the core keeps it in <ROM name>.mpk) */
EXPORT volatile int m64input_plugin = 1;

EXPORT int PluginStartup(m64p_dynlib_handle core, void *ctx, debugcb cb) {
    (void) core; (void) ctx; (void) cb;
    return 0;
}

EXPORT int PluginShutdown(void) {
    return 0;
}

EXPORT int PluginGetVersion(int *type, int *version, int *api, const char **name, int *caps) {
    if (type) *type = 4;         /* M64PLUGIN_INPUT */
    if (version) *version = 0x010000;
    if (api) *api = 0x020100;    /* INPUT_API_VERSION 2.1.0 */
    if (name) *name = "m64input (scripted)";
    if (caps) *caps = 0;
    return 0;
}

EXPORT void InitiateControllers(CONTROL_INFO info) {
    info.Controls[0].Present = 1;
    info.Controls[0].RawData = 0;
    info.Controls[0].Plugin = m64input_plugin; /* PLUGIN_NONE or PLUGIN_MEMPAK */
}

EXPORT void GetKeys(int control, uint32_t *keys) {
    if (control == 0) {
        *keys = m64input_keys;
        m64input_polls++;
    } else {
        *keys = 0;
    }
}

EXPORT void ControllerCommand(int control, unsigned char *cmd) { (void) control; (void) cmd; }
EXPORT void ReadController(int control, unsigned char *cmd) { (void) control; (void) cmd; }
EXPORT int RomOpen(void) { return 1; }
EXPORT void RomClosed(void) {}
EXPORT void SDL_KeyDown(int keymod, int keysym) { (void) keymod; (void) keysym; }
EXPORT void SDL_KeyUp(int keymod, int keysym) { (void) keymod; (void) keysym; }
EXPORT void RenderCallback(void) {}
