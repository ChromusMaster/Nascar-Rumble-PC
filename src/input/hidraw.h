/* Direct access to USB HID game controllers, bypassing the operating
 * system's gamepad drivers (DirectInput/XInput on Windows, evdev/joydev on
 * Linux). Reports are read straight from the device:
 *   Windows: SetupAPI enumeration + hid.dll (HidD_ and HidP_ functions) + ReadFile
 *   Linux:   /sys/class/hidraw + /dev/hidrawN + our descriptor parser
 * Other systems: enumeration returns nothing (use the system driver path). */
#ifndef NR_HIDRAW_H
#define NR_HIDRAW_H

#include "hidparse.h"

typedef struct {
    char path[512];        /* OS path to open */
    char name[128];        /* product name */
    u16 vid, pid;
    bool accessible;       /* false: present but cannot be opened (permissions) */
} nr_hid_devinfo;

/* Lists joystick/gamepad HID interfaces. Returns the number written. */
int nr_hidraw_enumerate(nr_hid_devinfo *out, int max);

typedef struct nr_hidraw nr_hidraw;

nr_hidraw *nr_hidraw_open(const char *path, char *err, size_t errlen);
/* Reads every pending report into st. Returns false when the device is gone. */
bool nr_hidraw_poll(nr_hidraw *h, nr_hid_state *st);
const nr_hid_layout *nr_hidraw_layout(const nr_hidraw *h);
void nr_hidraw_close(nr_hidraw *h);

#endif
