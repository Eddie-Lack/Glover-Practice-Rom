/* pulseinput: gloveworld's scriptinput plugin (tools/emu/scriptinput.c) with pulses added. A pulse presses buttons
 * for a number of controller polls (reads of the buttons through the PIF) instead of a number of video frames, so
 * a test can make a tap that is shorter than the time between two reads of the game.
 * Build: cc -shared -fPIC -O2 -I/usr/include/mupen64plus -o pulseinput.so pulseinput.c */
#define M64P_PLUGIN_PROTOTYPES 1
#include <stdlib.h>
#include <string.h>
#include "m64p_types.h"
#include "m64p_plugin.h"

static unsigned int buttons[4];
static unsigned int pulseValue[4];
static int pulsePolls[4];
static CONTROL *controls;

/* The button word of controller CTRL: the BUTTONS layout from m64p_plugin.h (bits 0 to 15 buttons, bits 16 to 23
 * the stick's X, bits 24 to 31 its Y). */
EXPORT void CALL scriptinput_set(int ctrl, unsigned int value)
{
    if (ctrl >= 0 && ctrl < 4)
        buttons[ctrl] = value;
}

/* Adds VALUE to the buttons for the next POLLS polls of controller CTRL. */
EXPORT void CALL scriptinput_pulse(int ctrl, unsigned int value, int polls)
{
    if (ctrl >= 0 && ctrl < 4) {
        pulseValue[ctrl] = value;
        pulsePolls[ctrl] = polls;
    }
}

/* The polls left of the pulse. */
EXPORT int CALL scriptinput_pulse_left(int ctrl)
{
    return ctrl >= 0 && ctrl < 4 ? pulsePolls[ctrl] : 0;
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *context, void (*debug)(void *, int, const char *))
{
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *version, int *api, const char **name, int *caps)
{
    if (type) *type = M64PLUGIN_INPUT;
    if (version) *version = 0x010000;
    if (api) *api = 0x020100;
    if (name) *name = "pulseinput";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

EXPORT void CALL InitiateControllers(CONTROL_INFO info)
{
    int i;

    controls = info.Controls;
    /* PULSEINPUT_PORTS is a list of controller numbers (1 to 4, as digits, "12" for two) that are present. The
     * core emulates a Controller Pak in each of them that PULSEINPUT_PAKS lists in the same way: it keeps the 32 KB
     * of the pak in a .mpk file in the save folder. */
    const char *ports = getenv("PULSEINPUT_PORTS");
    const char *paks = getenv("PULSEINPUT_PAKS");
    /* PULSEINPUT_RUMBLE lists the controllers that have a Rumble Pak, which keeps no data. */
    const char *rumble = getenv("PULSEINPUT_RUMBLE");

    for (i = 0; i < 4; i++) {
        char digit = '1' + i;

        controls[i].Present = ports ? strchr(ports, digit) != NULL : i == 0;
        controls[i].RawData = 0;
        controls[i].Plugin = paks && strchr(paks, digit) ? PLUGIN_MEMPAK
                           : rumble && strchr(rumble, digit) ? PLUGIN_RUMBLE_PAK : PLUGIN_NONE;
    }
}

EXPORT void CALL GetKeys(int ctrl, BUTTONS *keys)
{
    keys->Value = buttons[ctrl];
    if (pulsePolls[ctrl] > 0) {
        keys->Value |= pulseValue[ctrl];
        pulsePolls[ctrl]--;
    }
}

EXPORT void CALL ControllerCommand(int ctrl, unsigned char *command) {}
EXPORT void CALL ReadController(int ctrl, unsigned char *command) {}
EXPORT int CALL RomOpen(void) { return 1; }
EXPORT void CALL RomClosed(void) {}
EXPORT void CALL SDL_KeyDown(int keymod, int keysym) {}
EXPORT void CALL SDL_KeyUp(int keymod, int keysym) {}
