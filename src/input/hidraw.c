#include "hidraw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
/* ===================================================================== Windows */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

struct nr_hidraw {
    HANDLE h;
    OVERLAPPED ov;
    bool pending;
    PHIDP_PREPARSED_DATA pp;
    HIDP_CAPS caps;
    u8 *buf;
    nr_hid_layout layout;
    USHORT btn_min;            /* first button usage */
    USAGE *usages;
    ULONG max_usages;
};

static void wide_to_utf8(const wchar_t *w, char *out, size_t n)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) && n) out[0] = 0;
}

/* Builds a layout (counts, logical ranges, usages) from the preparsed data. */
static bool build_layout(PHIDP_PREPARSED_DATA pp, const HIDP_CAPS *caps, nr_hid_layout *l, USHORT *btn_min)
{
    HIDP_BUTTON_CAPS bc[16];
    HIDP_VALUE_CAPS vc[32];
    USHORT nb = 16, nv = 32, i;
    memset(l, 0, sizeof(*l));
    l->top_usage_page = caps->UsagePage;
    l->top_usage = caps->Usage;
    *btn_min = 0xFFFF;
    if (caps->NumberInputButtonCaps && HidP_GetButtonCaps(HidP_Input, bc, &nb, pp) == HIDP_STATUS_SUCCESS) {
        for (i = 0; i < nb; i++) {
            USHORT a, b, u;
            if (bc[i].UsagePage != 0x09) continue;
            a = bc[i].IsRange ? bc[i].Range.UsageMin : bc[i].NotRange.Usage;
            b = bc[i].IsRange ? bc[i].Range.UsageMax : bc[i].NotRange.Usage;
            if (a < *btn_min) *btn_min = a;
            for (u = a; u <= b && l->nbuttons < NR_HID_MAX_BUTTONS; u++) {
                l->button[l->nbuttons].usage_page = 0x09;
                l->button[l->nbuttons].usage = u;
                l->button[l->nbuttons].report_id = bc[i].ReportID;
                l->nbuttons++;
                if (u == 0xFFFF) break;
            }
        }
    }
    if (*btn_min == 0xFFFF) *btn_min = 1;
    if (caps->NumberInputValueCaps && HidP_GetValueCaps(HidP_Input, vc, &nv, pp) == HIDP_STATUS_SUCCESS) {
        for (i = 0; i < nv; i++) {
            USHORT u = vc[i].IsRange ? vc[i].Range.UsageMin : vc[i].NotRange.Usage;
            nr_hid_field f;
            memset(&f, 0, sizeof(f));
            f.usage_page = vc[i].UsagePage;
            f.usage = u;
            f.report_id = vc[i].ReportID;
            f.bit_size = (u8)vc[i].BitSize;
            f.logical_min = vc[i].LogicalMin;
            f.logical_max = vc[i].LogicalMax;
            if (f.logical_max < f.logical_min && f.bit_size < 32) f.logical_max = (s32)((1u << f.bit_size) - 1u);
            if (f.usage_page == 0x01 && u == 0x39) { if (l->nhats < NR_HID_MAX_HATS) l->hat[l->nhats++] = f; }
            else if (f.usage_page == 0x01 && u >= 0x30 && u <= 0x37) { if (l->naxes < NR_HID_MAX_AXES) l->axis[l->naxes++] = f; }
        }
    }
    return l->nbuttons > 0 || l->naxes > 0 || l->nhats > 0;
}

int nr_hidraw_enumerate(nr_hid_devinfo *out, int max)
{
    GUID g;
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA ifd;
    DWORD idx;
    int n = 0;
    HidD_GetHidGuid(&g);
    set = SetupDiGetClassDevsW(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;
    ifd.cbSize = sizeof(ifd);
    for (idx = 0; n < max && SetupDiEnumDeviceInterfaces(set, NULL, &g, idx, &ifd); idx++) {
        DWORD need = 0;
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *det;
        HANDLE h;
        HIDD_ATTRIBUTES at;
        PHIDP_PREPARSED_DATA pp = NULL;
        HIDP_CAPS caps;
        wchar_t prod[128];
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, NULL, 0, &need, NULL);
        if (!need) continue;
        det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)malloc(need);
        if (!det) break;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, NULL, NULL)) { free(det); continue; }
        /* access 0: attributes can be read even when the device is in use */
        h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            at.Size = sizeof(at);
            if (HidD_GetAttributes(h, &at) && HidD_GetPreparsedData(h, &pp)) {
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0x01 &&
                    (caps.Usage == 0x04 || caps.Usage == 0x05 || caps.Usage == 0x08)) {
                    nr_hid_devinfo *o = &out[n++];
                    HANDLE t;
                    memset(o, 0, sizeof(*o));
                    wide_to_utf8(det->DevicePath, o->path, sizeof(o->path));
                    o->vid = at.VendorID;
                    o->pid = at.ProductID;
                    if (HidD_GetProductString(h, prod, sizeof(prod))) wide_to_utf8(prod, o->name, sizeof(o->name));
                    if (!o->name[0]) snprintf(o->name, sizeof(o->name), "HID gamepad %04X:%04X", o->vid, o->pid);
                    t = CreateFileW(det->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                    o->accessible = t != INVALID_HANDLE_VALUE;
                    if (o->accessible) CloseHandle(t);
                }
                HidD_FreePreparsedData(pp);
            }
            CloseHandle(h);
        }
        free(det);
    }
    SetupDiDestroyDeviceInfoList(set);
    return n;
}

nr_hidraw *nr_hidraw_open(const char *path, char *err, size_t n)
{
    wchar_t wp[512];
    nr_hidraw *h;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 512)) { snprintf(err, n, "invalid device path"); return NULL; }
    h = (nr_hidraw *)calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->h = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h->h == INVALID_HANDLE_VALUE) { snprintf(err, n, "cannot open HID device (error %lu)", (unsigned long)GetLastError()); free(h); return NULL; }
    if (!HidD_GetPreparsedData(h->h, &h->pp) || HidP_GetCaps(h->pp, &h->caps) != HIDP_STATUS_SUCCESS ||
        !build_layout(h->pp, &h->caps, &h->layout, &h->btn_min)) {
        snprintf(err, n, "not a usable gamepad");
        nr_hidraw_close(h);
        return NULL;
    }
    h->buf = (u8 *)calloc(1, h->caps.InputReportByteLength + 1u);
    h->max_usages = HidP_MaxUsageListLength(HidP_Input, 0x09, h->pp);
    h->usages = (USAGE *)calloc(h->max_usages + 1u, sizeof(USAGE));
    h->ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!h->buf || !h->usages || !h->ov.hEvent) { snprintf(err, n, "out of memory"); nr_hidraw_close(h); return NULL; }
    return h;
}

static void decode_win(nr_hidraw *h, nr_hid_state *st, DWORD len)
{
    ULONG nu = h->max_usages, i;
    int k;
    PCHAR rep = (PCHAR)h->buf;
    if (h->layout.nbuttons) {
        if (HidP_GetUsages(HidP_Input, 0x09, 0, h->usages, &nu, h->pp, rep, len) == HIDP_STATUS_SUCCESS) {
            u32 bits = 0;
            for (i = 0; i < nu; i++) {
                int b = (int)h->usages[i] - (int)h->btn_min;
                if (b >= 0 && b < 32) bits |= 1u << b;
            }
            st->buttons = bits;
        }
    }
    for (k = 0; k < h->layout.naxes; k++) {
        const nr_hid_field *f = &h->layout.axis[k];
        ULONG v;
        if (HidP_GetUsageValue(HidP_Input, f->usage_page, 0, f->usage, &v, h->pp, rep, len) == HIDP_STATUS_SUCCESS) {
            s32 sv = (s32)v;
            if (f->logical_min < 0 && f->bit_size < 32 && (v & (1u << (f->bit_size - 1)))) sv = (s32)(v | (~0u << f->bit_size));
            st->axis[k] = nr_hid_norm_axis(sv, f->logical_min, f->logical_max);
        }
    }
    for (k = 0; k < h->layout.nhats; k++) {
        const nr_hid_field *f = &h->layout.hat[k];
        ULONG v;
        if (HidP_GetUsageValue(HidP_Input, f->usage_page, 0, f->usage, &v, h->pp, rep, len) == HIDP_STATUS_SUCCESS) {
            s32 r = (s32)v - f->logical_min;
            if (f->logical_max - f->logical_min == 3) st->hat[k] = (s8)((r >= 0 && r <= 3) ? r * 2 : -1);
            else st->hat[k] = (s8)((r >= 0 && r <= 7) ? r : -1);
        }
    }
}

bool nr_hidraw_poll(nr_hidraw *h, nr_hid_state *st)
{
    int guard;
    for (guard = 0; guard < 64; guard++) {
        DWORD got = 0;
        if (!h->pending) {
            ResetEvent(h->ov.hEvent);
            if (ReadFile(h->h, h->buf, h->caps.InputReportByteLength, &got, &h->ov)) {
                decode_win(h, st, got);
                continue;
            }
            if (GetLastError() != ERROR_IO_PENDING) return false;
            h->pending = true;
        }
        if (!GetOverlappedResult(h->h, &h->ov, &got, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) return true;   /* nothing new yet */
            h->pending = false;
            return false;
        }
        h->pending = false;
        decode_win(h, st, got);
    }
    return true;
}

const nr_hid_layout *nr_hidraw_layout(const nr_hidraw *h) { return &h->layout; }

void nr_hidraw_close(nr_hidraw *h)
{
    if (!h) return;
    if (h->h && h->h != INVALID_HANDLE_VALUE) { CancelIo(h->h); CloseHandle(h->h); }
    if (h->ov.hEvent) CloseHandle(h->ov.hEvent);
    if (h->pp) HidD_FreePreparsedData(h->pp);
    free(h->buf);
    free(h->usages);
    free(h);
}

#elif defined(__linux__)
/* ======================================================================= Linux */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/hidraw.h>

struct nr_hidraw {
    int fd;
    nr_hid_layout layout;
};

static long read_small(const char *path, u8 *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return -1;
    n = fread(buf, 1, cap, f);
    fclose(f);
    return (long)n;
}

int nr_hidraw_enumerate(nr_hid_devinfo *out, int max)
{
    DIR *d = opendir("/sys/class/hidraw");
    struct dirent *e;
    int n = 0;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL && n < max) {
        char p[512];
        u8 desc[4096], ue[2048];
        long dl, ul;
        nr_hid_layout lay;
        nr_hid_devinfo *o;
        if (strncmp(e->d_name, "hidraw", 6) != 0) continue;
        snprintf(p, sizeof(p), "/sys/class/hidraw/%s/device/report_descriptor", e->d_name);
        dl = read_small(p, desc, sizeof(desc));
        if (dl <= 0 || !nr_hid_parse(desc, (size_t)dl, &lay)) continue;
        o = &out[n++];
        memset(o, 0, sizeof(*o));
        snprintf(o->path, sizeof(o->path), "/dev/%s", e->d_name);
        snprintf(p, sizeof(p), "/sys/class/hidraw/%s/device/uevent", e->d_name);
        ul = read_small(p, ue, sizeof(ue) - 1);
        if (ul > 0) {
            char *s, *line;
            ue[ul] = 0;
            for (line = (char *)ue; line && *line; line = s) {
                s = strchr(line, '\n');
                if (s) *s++ = 0;
                if (strncmp(line, "HID_ID=", 7) == 0) {
                    unsigned bus, vid, pid;
                    if (sscanf(line + 7, "%x:%x:%x", &bus, &vid, &pid) == 3) { o->vid = (u16)vid; o->pid = (u16)pid; }
                } else if (strncmp(line, "HID_NAME=", 9) == 0) {
                    nr_strlcpy(o->name, line + 9, sizeof(o->name));
                }
            }
        }
        if (!o->name[0]) snprintf(o->name, sizeof(o->name), "HID gamepad %04X:%04X", o->vid, o->pid);
        o->accessible = access(o->path, R_OK) == 0;
    }
    closedir(d);
    return n;
}

nr_hidraw *nr_hidraw_open(const char *path, char *err, size_t n)
{
    nr_hidraw *h;
    int sz = 0;
    struct hidraw_report_descriptor rd;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM)
            snprintf(err, n, "permission denied for %s (install the udev rule from the package, see docs/CONTROLLERS.md)", path);
        else snprintf(err, n, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }
    if (ioctl(fd, HIDIOCGRDESCSIZE, &sz) < 0 || sz <= 0 || sz > HID_MAX_DESCRIPTOR_SIZE) { snprintf(err, n, "cannot read the report descriptor"); close(fd); return NULL; }
    memset(&rd, 0, sizeof(rd));
    rd.size = (unsigned)sz;
    if (ioctl(fd, HIDIOCGRDESC, &rd) < 0) { snprintf(err, n, "cannot read the report descriptor"); close(fd); return NULL; }
    h = (nr_hidraw *)calloc(1, sizeof(*h));
    if (!h) { close(fd); return NULL; }
    h->fd = fd;
    if (!nr_hid_parse(rd.value, rd.size, &h->layout)) { snprintf(err, n, "not a usable gamepad"); nr_hidraw_close(h); return NULL; }
    return h;
}

bool nr_hidraw_poll(nr_hidraw *h, nr_hid_state *st)
{
    u8 buf[256];
    int guard;
    for (guard = 0; guard < 64; guard++) {
        ssize_t r = read(h->fd, buf, sizeof(buf));
        if (r > 0) { nr_hid_decode(&h->layout, buf, (size_t)r, st); continue; }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return true;
        return false;                       /* 0 or error: unplugged */
    }
    return true;
}

const nr_hid_layout *nr_hidraw_layout(const nr_hidraw *h) { return &h->layout; }

void nr_hidraw_close(nr_hidraw *h)
{
    if (!h) return;
    if (h->fd >= 0) close(h->fd);
    free(h);
}

#else
/* ====================================================================== other */
struct nr_hidraw { nr_hid_layout layout; };
int nr_hidraw_enumerate(nr_hid_devinfo *out, int max) { (void)out; (void)max; return 0; }
nr_hidraw *nr_hidraw_open(const char *path, char *err, size_t n) { (void)path; snprintf(err, n, "direct HID access is not available on this system"); return NULL; }
bool nr_hidraw_poll(nr_hidraw *h, nr_hid_state *st) { (void)h; (void)st; return false; }
const nr_hid_layout *nr_hidraw_layout(const nr_hidraw *h) { return &h->layout; }
void nr_hidraw_close(nr_hidraw *h) { (void)h; }
#endif
