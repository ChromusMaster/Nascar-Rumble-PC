#include "input.h"
#include "hidraw.h"
#include "../app/app.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const nr_ps_button_key[NR_PS_BUTTONS] = {
    "up", "down", "left", "right", "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "start", "select"
};
const u16 nr_ps_button_bit[NR_PS_BUTTONS] = {
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE,
    PAD_L1, PAD_R1, PAD_L2, PAD_R2, PAD_START, PAD_SELECT
};

/* Standard layout (SDL game controller names): the console's face buttons
 * sit where the PlayStation ones do (south = cross, east = circle...). */
static const char *const k_mapped_default[NR_PS_BUTTONS] = {
    "dpup", "dpdown", "dpleft", "dpright", "a", "b", "x", "y",
    "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger", "start", "back"
};
/* Generic USB pads and PS1/PS2-to-USB adapters ("PC Twin Shock" boards,
 * DragonRise 0079:0006, 0810:0001...) number their buttons 1 triangle,
 * 2 circle, 3 cross, 4 square, 5 L2, 6 R2, 7 L1, 8 R1, 9 select, 10 start
 * and report the D-pad as a hat or as the first two axes. Clones differ, so
 * every binding can be changed in the launcher. */
static const char *const k_raw_default[NR_PS_BUTTONS] = {
    "h0.up", "h0.down", "h0.left", "h0.right", "b2", "b1", "b3", "b0",
    "b6", "b7", "b4", "b5", "b9", "b8"
};
static const char *const k_key_default[NR_PS_BUTTONS] = {
    "Up", "Down", "Left", "Right", "Z", "X", "A", "S", "Q", "W", "E", "R", "Return", "Space"
};

#define AXIS_ON 16000

/* ------------------------------------------------------------ devices */

int nr_input_list(nr_input_device *out, int max)
{
    int n = 0, i, j;
    nr_hid_devinfo hid[NR_MAX_INPUT_DEVICES];
    int nh;
    for (i = 0; i < SDL_NumJoysticks() && n < max; i++) {
        nr_input_device *d = &out[n];
        SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(i);
        char gs[40];
        int same = 0;
        const char *nm;
        memset(d, 0, sizeof(*d));
        SDL_JoystickGetGUIDString(g, gs, sizeof(gs));
        for (j = 0; j < n; j++) if (out[j].kind == NR_DEV_SDL && strncmp(out[j].id + 4, gs, strlen(gs)) == 0) same++;
        d->kind = NR_DEV_SDL;
        snprintf(d->id, sizeof(d->id), "sdl:%s@%d", gs, same);
        d->mapped = SDL_IsGameController(i) == SDL_TRUE;
        nm = d->mapped ? SDL_GameControllerNameForIndex(i) : SDL_JoystickNameForIndex(i);
        nr_strlcpy(d->name, nm ? nm : "Controller", sizeof(d->name));
        d->vid = SDL_JoystickGetDeviceVendor(i);
        d->pid = SDL_JoystickGetDeviceProduct(i);
        d->sdl_index = i;
        d->accessible = true;
        n++;
    }
    nh = nr_hidraw_enumerate(hid, NR_MAX_INPUT_DEVICES);
    for (i = 0; i < nh && n < max; i++) {
        nr_input_device *d = &out[n];
        int same = 0;
        for (j = 0; j < i; j++) if (hid[j].vid == hid[i].vid && hid[j].pid == hid[i].pid) same++;
        memset(d, 0, sizeof(*d));
        d->kind = NR_DEV_HID;
        snprintf(d->id, sizeof(d->id), "hid:%04x:%04x@%d", hid[i].vid, hid[i].pid, same);
        nr_strlcpy(d->name, hid[i].name, sizeof(d->name));
        nr_strlcpy(d->path, hid[i].path, sizeof(d->path));
        d->vid = hid[i].vid;
        d->pid = hid[i].pid;
        d->accessible = hid[i].accessible;
        d->sdl_index = -1;
        n++;
    }
    return n;
}

struct nr_pad {
    nr_dev_kind kind;
    char name[128];
    SDL_GameController *gc;
    SDL_Joystick *js;
    SDL_JoystickID jid;
    nr_hidraw *hid;
    nr_hid_state st;
    bool alive;
    /* capture baseline */
    bool cap_active;
    u8 base_btn[64];
    s16 base_axis[16];
    u8 base_hat[4];
};

nr_pad *nr_pad_open(const char *id, int auto_index)
{
    nr_input_device dev[NR_MAX_INPUT_DEVICES];
    int n = nr_input_list(dev, NR_MAX_INPUT_DEVICES), i, pick = -1;
    nr_pad *p;
    if (!id || !*id || nr_strcasecmp(id, "none") == 0 || nr_strcasecmp(id, "keyboard") == 0) return NULL;
    if (nr_strcasecmp(id, "auto") == 0) {
        int k = 0;
        for (i = 0; i < n; i++) if (dev[i].kind == NR_DEV_SDL) { if (k == auto_index) { pick = i; break; } k++; }
    } else {
        for (i = 0; i < n; i++) if (strcmp(dev[i].id, id) == 0) { pick = i; break; }
    }
    if (pick < 0) return NULL;
    p = (nr_pad *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->kind = dev[pick].kind;
    nr_strlcpy(p->name, dev[pick].name, sizeof(p->name));
    if (p->kind == NR_DEV_SDL) {
        if (dev[pick].mapped) {
            p->gc = SDL_GameControllerOpen(dev[pick].sdl_index);
            p->js = p->gc ? SDL_GameControllerGetJoystick(p->gc) : NULL;
        }
        if (!p->js) p->js = SDL_JoystickOpen(dev[pick].sdl_index);
        if (!p->js) { free(p); return NULL; }
        p->jid = SDL_JoystickInstanceID(p->js);
    } else {
        char err[256];
        p->hid = nr_hidraw_open(dev[pick].path, err, sizeof(err));
        if (!p->hid) { NR_WARN("input", "%s: %s", dev[pick].name, err); free(p); return NULL; }
        nr_hid_state_reset(&p->st);
    }
    p->alive = true;
    return p;
}

void nr_pad_close(nr_pad *p)
{
    if (!p) return;
    if (p->gc) SDL_GameControllerClose(p->gc);
    else if (p->js) SDL_JoystickClose(p->js);
    if (p->hid) nr_hidraw_close(p->hid);
    free(p);
}

const char *nr_pad_name(const nr_pad *p) { return p ? p->name : ""; }
bool nr_pad_mapped(const nr_pad *p) { return p && p->gc != NULL; }

void nr_pad_update(nr_pad *p)
{
    if (!p) return;
    if (p->hid && p->alive) p->alive = nr_hidraw_poll(p->hid, &p->st);
    if (p->js && !SDL_JoystickGetAttached(p->js)) p->alive = false;
}

/* ------------------------------------------------------------ raw access */

static int raw_buttons(const nr_pad *p)
{
    if (p->hid) return nr_hidraw_layout(p->hid)->nbuttons;
    return p->js ? SDL_JoystickNumButtons(p->js) : 0;
}
static int raw_axes(const nr_pad *p)
{
    if (p->hid) return nr_hidraw_layout(p->hid)->naxes;
    return p->js ? SDL_JoystickNumAxes(p->js) : 0;
}
static int raw_hats(const nr_pad *p)
{
    if (p->hid) return nr_hidraw_layout(p->hid)->nhats;
    return p->js ? SDL_JoystickNumHats(p->js) : 0;
}
static bool raw_button(const nr_pad *p, int i)
{
    if (p->hid) return i < 32 && (p->st.buttons >> i) & 1;
    return p->js && SDL_JoystickGetButton(p->js, i);
}
static s16 raw_axis(const nr_pad *p, int i)
{
    if (p->hid) return i < NR_HID_MAX_AXES ? p->st.axis[i] : 0;
    return p->js ? SDL_JoystickGetAxis(p->js, i) : 0;
}
/* hat as SDL bit mask (SDL_HAT_UP=1, RIGHT=2, DOWN=4, LEFT=8) */
static u8 raw_hat(const nr_pad *p, int i)
{
    if (p->hid) {
        static const u8 dirs[8] = { 1, 1 | 2, 2, 2 | 4, 4, 4 | 8, 8, 8 | 1 };
        int v = i < NR_HID_MAX_HATS ? p->st.hat[i] : -1;
        return (u8)(v >= 0 && v < 8 ? dirs[v] : 0);
    }
    return p->js ? SDL_JoystickGetHat(p->js, i) : 0;
}

static u8 hat_bit(const char *dir)
{
    if (strcmp(dir, "up") == 0) return SDL_HAT_UP;
    if (strcmp(dir, "down") == 0) return SDL_HAT_DOWN;
    if (strcmp(dir, "left") == 0) return SDL_HAT_LEFT;
    if (strcmp(dir, "right") == 0) return SDL_HAT_RIGHT;
    return 0;
}

bool nr_pad_binding_active(const nr_pad *p, const char *b)
{
    if (!p || !p->alive || !b || !*b) return false;
    if (b[0] == 'b' && b[1] >= '0' && b[1] <= '9') return raw_button(p, atoi(b + 1));
    if (b[0] == 'a' && b[1] >= '0' && b[1] <= '9') {
        int i = atoi(b + 1);
        const char *s = b + 1;
        s16 v = raw_axis(p, i);
        while (*s >= '0' && *s <= '9') s++;
        return *s == '-' ? v < -AXIS_ON : v > AXIS_ON;
    }
    if (b[0] == 'h' && b[1] >= '0' && b[1] <= '9') {
        const char *dot = strchr(b, '.');
        return dot && (raw_hat(p, atoi(b + 1)) & hat_bit(dot + 1));
    }
    if (p->gc) {
        char name[32];
        size_t l = strlen(b);
        char sign = 0;
        SDL_GameControllerButton btn;
        SDL_GameControllerAxis ax;
        nr_strlcpy(name, b, sizeof(name));
        if (l > 1 && (b[l - 1] == '+' || b[l - 1] == '-')) { sign = b[l - 1]; name[l - 1] = 0; }
        btn = SDL_GameControllerGetButtonFromString(name);
        if (btn != SDL_CONTROLLER_BUTTON_INVALID) return SDL_GameControllerGetButton(p->gc, btn) != 0;
        ax = SDL_GameControllerGetAxisFromString(name);
        if (ax != SDL_CONTROLLER_AXIS_INVALID) {
            s16 v = SDL_GameControllerGetAxis(p->gc, ax);
            return sign == '-' ? v < -AXIS_ON : v > AXIS_ON;
        }
    }
    return false;
}

const char *nr_pad_default_binding(const nr_pad *p, int b)
{
    if (b < 0 || b >= NR_PS_BUTTONS) return "";
    if (p && p->gc) return k_mapped_default[b];
    if (p && b < 4 && raw_hats(p) == 0) {
        static const char *const ax[4] = { "a1-", "a1+", "a0-", "a0+" };   /* D-pad reported as axes */
        return ax[b];
    }
    return k_raw_default[b];
}

void nr_pad_capture_begin(nr_pad *p)
{
    int i;
    if (!p) return;
    p->cap_active = true;
    if (p->gc) {
        for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 64; i++) p->base_btn[i] = SDL_GameControllerGetButton(p->gc, (SDL_GameControllerButton)i);
        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX && i < 16; i++) p->base_axis[i] = SDL_GameControllerGetAxis(p->gc, (SDL_GameControllerAxis)i);
        return;
    }
    for (i = 0; i < raw_buttons(p) && i < 64; i++) p->base_btn[i] = raw_button(p, i);
    for (i = 0; i < raw_axes(p) && i < 16; i++) p->base_axis[i] = raw_axis(p, i);
    for (i = 0; i < raw_hats(p) && i < 4; i++) p->base_hat[i] = raw_hat(p, i);
}

static bool axis_newly(s16 base, s16 v, char *sign)
{
    if (v > AXIS_ON && base <= AXIS_ON) { *sign = '+'; return true; }
    if (v < -AXIS_ON && base >= -AXIS_ON) { *sign = '-'; return true; }
    return false;
}

bool nr_pad_capture(nr_pad *p, char *out, size_t n)
{
    int i;
    char sign;
    if (!p || !p->cap_active || !p->alive) return false;
    if (p->gc) {
        for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 64; i++) {
            u8 v = SDL_GameControllerGetButton(p->gc, (SDL_GameControllerButton)i);
            if (v && !p->base_btn[i]) {
                snprintf(out, n, "%s", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)i));
                p->cap_active = false;
                return true;
            }
            p->base_btn[i] = v;
        }
        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX && i < 16; i++) {
            s16 v = SDL_GameControllerGetAxis(p->gc, (SDL_GameControllerAxis)i);
            if (axis_newly(p->base_axis[i], v, &sign)) {
                bool trigger = i == SDL_CONTROLLER_AXIS_TRIGGERLEFT || i == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
                snprintf(out, n, "%s%s", SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)i), trigger ? "" : (sign == '+' ? "+" : "-"));
                p->cap_active = false;
                return true;
            }
            if (v > -AXIS_ON / 2 && v < AXIS_ON / 2) p->base_axis[i] = v;   /* re-arm when centred */
        }
        return false;
    }
    for (i = 0; i < raw_buttons(p) && i < 64; i++) {
        u8 v = raw_button(p, i);
        if (v && !p->base_btn[i]) { snprintf(out, n, "b%d", i); p->cap_active = false; return true; }
        p->base_btn[i] = v;
    }
    for (i = 0; i < raw_hats(p) && i < 4; i++) {
        u8 v = raw_hat(p, i), nw = (u8)(v & ~p->base_hat[i]);
        if (nw) {
            const char *d = (nw & SDL_HAT_UP) ? "up" : (nw & SDL_HAT_DOWN) ? "down" : (nw & SDL_HAT_LEFT) ? "left" : "right";
            snprintf(out, n, "h%d.%s", i, d);
            p->cap_active = false;
            return true;
        }
        p->base_hat[i] = v;
    }
    for (i = 0; i < raw_axes(p) && i < 16; i++) {
        s16 v = raw_axis(p, i);
        if (axis_newly(p->base_axis[i], v, &sign)) { snprintf(out, n, "a%d%c", i, sign); p->cap_active = false; return true; }
        if (v > -AXIS_ON / 2 && v < AXIS_ON / 2) p->base_axis[i] = v;
    }
    return false;
}

/* ------------------------------------------------------------ config */

const char *nr_key_default(int port, int b)
{
    if (port != 0 || b < 0 || b >= NR_PS_BUTTONS) return "";
    return k_key_default[b];
}

void nr_port_config_load(nr_port_config *pc, const nr_config *cfg, int port)
{
    int i;
    char key[64];
    const char *ksec = port == 0 ? "input" : "input2";
    const char *psec = port == 0 ? "pad1" : "pad2";
    memset(pc, 0, sizeof(*pc));
    for (i = 0; i < NR_PS_BUTTONS; i++) {
        const char *name;
        snprintf(key, sizeof(key), "%s.%s", ksec, nr_ps_button_key[i]);
        name = nr_config_get(cfg, key, nr_key_default(port, i));
        if (name && *name) {
            SDL_Scancode sc = SDL_GetScancodeFromName(name);
            if (sc == SDL_SCANCODE_UNKNOWN) NR_WARN("input", "unknown key '%s' for %s", name, key);
            pc->keys[i] = (int)sc;
        }
        snprintf(key, sizeof(key), "%s.%s", psec, nr_ps_button_key[i]);
        nr_strlcpy(pc->bind[i], nr_config_get(cfg, key, ""), sizeof(pc->bind[i]));
    }
    snprintf(key, sizeof(key), "%s.device", psec);
    nr_strlcpy(pc->device, nr_config_get(cfg, key, "auto"), sizeof(pc->device));
    snprintf(key, sizeof(key), "%s.left_stick_dpad", psec);
    pc->stick_as_dpad = nr_config_get_int(cfg, key, 1) != 0;
}

void nr_port_config_save(const nr_port_config *pc, const char *ini, int port)
{
    int i;
    char key[64];
    const char *ksec = port == 0 ? "input" : "input2";
    const char *psec = port == 0 ? "pad1" : "pad2";
    for (i = 0; i < NR_PS_BUTTONS; i++) {
        snprintf(key, sizeof(key), "%s.%s", ksec, nr_ps_button_key[i]);
        nr_config_update_file(ini, key, pc->keys[i] ? SDL_GetScancodeName((SDL_Scancode)pc->keys[i]) : "");
        snprintf(key, sizeof(key), "%s.%s", psec, nr_ps_button_key[i]);
        nr_config_update_file(ini, key, pc->bind[i]);
    }
    snprintf(key, sizeof(key), "%s.device", psec);
    nr_config_update_file(ini, key, pc->device);
    snprintf(key, sizeof(key), "%s.left_stick_dpad", psec);
    nr_config_update_file(ini, key, pc->stick_as_dpad ? "1" : "0");
}

u16 nr_port_read(const nr_port_config *pc, nr_pad *pad, const u8 *ks)
{
    u16 m = 0;
    int i;
    for (i = 0; i < NR_PS_BUTTONS; i++) {
        if (ks && pc->keys[i] > 0 && pc->keys[i] < SDL_NUM_SCANCODES && ks[pc->keys[i]]) m |= nr_ps_button_bit[i];
        if (pad) {
            const char *b = pc->bind[i][0] ? pc->bind[i] : nr_pad_default_binding(pad, i);
            if (nr_pad_binding_active(pad, b)) m |= nr_ps_button_bit[i];
        }
    }
    if (pad && pc->stick_as_dpad) {
        static const char *const mapped[4] = { "lefty-", "lefty+", "leftx-", "leftx+" };
        static const char *const raw[4] = { "a1-", "a1+", "a0-", "a0+" };
        for (i = 0; i < 4; i++)
            if (nr_pad_binding_active(pad, pad->gc ? mapped[i] : raw[i])) m |= nr_ps_button_bit[i];
    }
    return m;
}
