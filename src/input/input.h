/* Controller input shared by the engine window and the launcher.
 *
 * Two ways to read a USB controller:
 *  - "sdl:"  the operating system's driver through SDL2 (XInput, DirectInput,
 *            RawInput, evdev, IOKit...). Controllers known to SDL's mapping
 *            database are used with their standard layout; any other generic
 *            joystick is used by raw button/axis/hat numbers.
 *  - "hid:"  direct USB HID access (hidraw.h): reports are read from the
 *            device itself and decoded from its report descriptor.
 *
 * Bindings (one per console button) are text, so they live in nrengine.ini:
 *   keyboard     SDL key name             e.g. "Z", "Left Shift"
 *   mapped pad   SDL controller name      e.g. "a", "dpup", "lefttrigger", "leftx-"
 *   raw device   b<n> | a<n>+ | a<n>- | h<n>.up|down|left|right
 */
#ifndef NR_INPUT_H
#define NR_INPUT_H

#include "../common/common.h"

#define NR_PS_BUTTONS 14
#define NR_MAX_INPUT_DEVICES 16

/* console buttons in configuration order */
extern const char *const nr_ps_button_key[NR_PS_BUTTONS];   /* "up", "down", ... "select" */
extern const u16 nr_ps_button_bit[NR_PS_BUTTONS];           /* PAD_* masks (app.h) */

typedef enum { NR_DEV_SDL = 1, NR_DEV_HID = 2 } nr_dev_kind;

typedef struct {
    nr_dev_kind kind;
    char id[160];        /* "sdl:<guid>@<n>" or "hid:<vid>:<pid>@<n>" */
    char name[128];
    u16 vid, pid;
    bool mapped;         /* SDL knows its layout (standard bindings available) */
    bool accessible;     /* HID: false when the OS denies access */
    int sdl_index;       /* SDL device index (SDL devices) */
    char path[512];      /* HID device path */
} nr_input_device;

/* SDL's joystick and game-controller subsystems must be initialised. */
int nr_input_list(nr_input_device *out, int max);

typedef struct nr_pad nr_pad;

/* "auto" picks the n-th connected SDL device (n = 0 for port 1, 1 for port 2) */
nr_pad *nr_pad_open(const char *id, int auto_index);
void nr_pad_close(nr_pad *p);
const char *nr_pad_name(const nr_pad *p);
bool nr_pad_mapped(const nr_pad *p);
/* refresh state (HID: read reports; SDL: state is updated by SDL events) */
void nr_pad_update(nr_pad *p);
bool nr_pad_binding_active(const nr_pad *p, const char *binding);
/* Binding capture for the launcher: call nr_pad_capture_begin, then
 * nr_pad_capture every frame; it returns true and the binding text when an
 * input that was idle at the start becomes active. */
void nr_pad_capture_begin(nr_pad *p);
bool nr_pad_capture(nr_pad *p, char *binding, size_t n);
/* default binding of a console button for this device */
const char *nr_pad_default_binding(const nr_pad *p, int ps_button);

/* Keyboard */
const char *nr_key_default(int port, int ps_button);   /* "" when none */

/* Per-port configuration read from nrengine.ini ([input]/[input2] keys,
 * [pad1]/[pad2] device and bindings). */
typedef struct {
    int keys[NR_PS_BUTTONS];                  /* SDL_Scancode, 0 = none */
    char device[160];                         /* "auto", "none", sdl:..., hid:... */
    char bind[NR_PS_BUTTONS][32];             /* device bindings ("" = device default) */
    bool stick_as_dpad;                       /* left stick also moves the D-pad */
} nr_port_config;

void nr_port_config_load(nr_port_config *pc, const nr_config *cfg, int port);
/* writes the port configuration back with nr_config_update_file */
void nr_port_config_save(const nr_port_config *pc, const char *ini_path, int port);

/* Button mask (PAD_* bits, 1 = pressed) of a port: keyboard + device. */
u16 nr_port_read(const nr_port_config *pc, nr_pad *pad, const u8 *keyboard_state);

#endif
