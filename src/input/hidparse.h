/* Minimal USB HID report-descriptor parser for game controllers.
 *
 * Used by the direct (raw HID) input path on systems where the report
 * descriptor is available (Linux hidraw). It extracts the input fields a
 * gamepad needs - buttons, axes and hat switches - with their bit position in
 * each input report, so reports can be decoded without the operating system's
 * gamepad driver. Windows decodes reports with its own HidP_* parser instead
 * (see hidraw.c), but exposes them through the same nr_hid_layout. */
#ifndef NR_HIDPARSE_H
#define NR_HIDPARSE_H

#include "../common/common.h"

#define NR_HID_MAX_BUTTONS 32
#define NR_HID_MAX_AXES 8
#define NR_HID_MAX_HATS 2

typedef struct {
    u8 report_id;          /* 0 when the device does not use report IDs */
    u16 bit_offset;        /* from the start of the report data (after the ID byte) */
    u8 bit_size;
    s32 logical_min, logical_max;
    u16 usage_page, usage;
} nr_hid_field;

typedef struct {
    nr_hid_field button[NR_HID_MAX_BUTTONS];
    int nbuttons;
    nr_hid_field axis[NR_HID_MAX_AXES];     /* X, Y, Z, Rx, Ry, Rz, slider, dial (in descriptor order) */
    int naxes;
    nr_hid_field hat[NR_HID_MAX_HATS];
    int nhats;
    bool uses_report_ids;
    u16 top_usage_page, top_usage;          /* first application collection */
} nr_hid_layout;

/* Parses a report descriptor. Returns false if it is malformed or has no
 * joystick/gamepad application collection. */
bool nr_hid_parse(const u8 *desc, size_t len, nr_hid_layout *out);

/* Decoded state of one report: buttons as a bit mask, axes normalised to
 * -32768..32767, hats as 0..7 (N, NE, E, ...) or -1 (centred). Only fields
 * belonging to the report's ID are updated. */
typedef struct {
    u32 buttons;
    s16 axis[NR_HID_MAX_AXES];
    s8 hat[NR_HID_MAX_HATS];
} nr_hid_state;

void nr_hid_state_reset(nr_hid_state *s);
void nr_hid_decode(const nr_hid_layout *l, const u8 *report, size_t len, nr_hid_state *s);
/* helper shared with the Windows path: logical value -> -32768..32767 */
s16 nr_hid_norm_axis(s32 v, s32 lmin, s32 lmax);

#endif
