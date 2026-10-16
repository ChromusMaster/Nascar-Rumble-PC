#include "lui.h"

#include <stdio.h>
#include <string.h>

static const char *g_pending_help;

int lui_px(const lui *u, int v) { return (int)(v * u->k + (v >= 0 ? 0.5f : -0.5f)); }

lui_rect lui_r(const lui *u, int x, int y, int w, int h)
{
    lui_rect r;
    r.x = lui_px(u, x); r.y = lui_px(u, y); r.w = lui_px(u, w); r.h = lui_px(u, h);
    return r;
}

void lui_begin(lui *u, int w, int h)
{
    u->next_id = 0;
    u->help = NULL;
    u->help_from_focus = false;
    g_pending_help = NULL;
    (void)w; (void)h;
    if (u->focus >= u->count_last && u->count_last > 0) u->focus = u->count_last - 1;
}

void lui_fill(lui *u, lui_rect r, u32 c)
{
    int x, y, x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w > u->s.w ? u->s.w : r.x + r.w, y1 = r.y + r.h > u->s.h ? u->s.h : r.y + r.h;
    u32 a = c >> 24;
    for (y = y0; y < y1; y++) {
        u32 *p = u->s.px + (size_t)y * u->s.w;
        if (a == 255) { for (x = x0; x < x1; x++) p[x] = c; continue; }
        for (x = x0; x < x1; x++) {
            u32 d = p[x], ia = 255 - a;
            u32 rr = ((c & 0xFF) * a + (d & 0xFF) * ia) / 255;
            u32 gg = (((c >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia) / 255;
            u32 bb = (((c >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia) / 255;
            p[x] = rr | (gg << 8) | (bb << 16) | 0xFF000000u;
        }
    }
}

void lui_frame(lui *u, lui_rect r, u32 c, int t)
{
    lui_rect a;
    a = r; a.h = t; lui_fill(u, a, c);
    a.y = r.y + r.h - t; lui_fill(u, a, c);
    a = r; a.w = t; lui_fill(u, a, c);
    a.x = r.x + r.w - t; lui_fill(u, a, c);
}

void lui_text_px(lui *u, int x, int y, int px, u32 c, const char *t) { lt_draw(&u->s, x, y, px, c, t); }
void lui_text(lui *u, int x, int y, int size, u32 c, const char *t) { lt_draw(&u->s, lui_px(u, x), lui_px(u, y), lui_px(u, size), c, t); }
int lui_text_w(const lui *u, int size, const char *t) { return lt_width(lui_px(u, size), t); }

void lui_text_fit(lui *u, int x, int y, int px, u32 c, const char *t, int max_w)
{
    char buf[512];
    size_t l;
    if (lt_width(px, t) <= max_w) { lt_draw(&u->s, x, y, px, c, t); return; }
    nr_strlcpy(buf, t, sizeof(buf) - 4);
    l = strlen(buf);
    while (l > 0) {
        /* drop one UTF-8 character */
        do { l--; } while (l > 0 && ((unsigned char)buf[l] & 0xC0) == 0x80);
        buf[l] = 0;
        strcat(buf, "...");
        if (lt_width(px, buf) <= max_w) break;
        buf[l] = 0;
    }
    lt_draw(&u->s, x, y, px, c, buf);
}

bool lui_hover(const lui *u, lui_rect r)
{
    return u->popup_id < 0 && u->mx >= r.x && u->my >= r.y && u->mx < r.x + r.w && u->my < r.y + r.h;
}

void lui_help(lui *u, const char *help) { (void)u; g_pending_help = help; }

/* registers a focusable control; returns its id */
static int control(lui *u, lui_rect r, bool *focused, bool *hot)
{
    int id = u->next_id++;
    *hot = lui_hover(u, r);
    if (*hot && u->mpressed) { u->focus = id; u->focus_visible = false; }
    *focused = u->focus == id && u->popup_id < 0;
    if (g_pending_help) {
        if (*hot) u->help = g_pending_help;
        else if (*focused && !u->help) { u->help = g_pending_help; u->help_from_focus = true; }
        g_pending_help = NULL;
    }
    if (*focused && u->scroll_to_focus) u->scroll_to_focus = 0;
    return id;
}

static bool take_key(lui *u, bool focused, lui_key k)
{
    if (!focused || u->key_used || u->key != k) return false;
    u->key_used = true;
    return true;
}

static void focus_ring(lui *u, lui_rect r, bool focused)
{
    if (focused && u->focus_visible) {
        lui_rect o;
        o.x = r.x - 2; o.y = r.y - 2; o.w = r.w + 4; o.h = r.h + 4;
        lui_frame(u, o, LUI_ACCENT, 2);
    }
}

static int text_px(const lui *u) { return lui_px(u, 15); }

int lui_combo(lui *u, lui_rect r, const char *const *items, int n, int cur)
{
    bool f, hot;
    int id = control(u, r, &f, &hot);
    int px = text_px(u), arrow;
    if (u->popup_result_id == id) { cur = u->popup_result; u->popup_result_id = -1; }
    if (cur < 0 || cur >= n) cur = 0;
    if (take_key(u, f, LK_LEFT) && cur > 0) cur--;
    if (take_key(u, f, LK_RIGHT) && cur + 1 < n) cur++;
    if ((hot && u->mreleased) || take_key(u, f, LK_ENTER)) {
        u->popup_id = id;
        u->popup_anchor = r;
        u->popup_n = n < LUI_POPUP_MAX ? n : LUI_POPUP_MAX;
        u->popup_sel = cur;
        u->popup_scroll = 0;
        u->mreleased = false;
    }
    if (u->popup_id == id) {     /* refresh the copies every frame while open */
        int i;
        u->popup_n = n < LUI_POPUP_MAX ? n : LUI_POPUP_MAX;
        for (i = 0; i < u->popup_n; i++) nr_strlcpy(u->popup_text[i], items[i] ? items[i] : "", LUI_POPUP_TEXT);
        if (u->popup_sel >= u->popup_n) u->popup_sel = u->popup_n - 1;
    }
    lui_fill(u, r, hot || f ? LUI_FIELD_HOT : LUI_FIELD);
    lui_frame(u, r, f ? LUI_ACCENT : 0xFF5A524Cu, 1);
    arrow = lui_px(u, 18);
    if (n > 0) lui_text_fit(u, r.x + lui_px(u, 8), r.y + (r.h - lt_line_height(px)) / 2 + 1, px, LUI_TEXT, items[cur], r.w - arrow - lui_px(u, 12));
    {   /* down arrow */
        int aw = lui_px(u, 10), ah = lui_px(u, 5), ax = r.x + r.w - arrow, ay = r.y + (r.h - ah) / 2, j;
        for (j = 0; j < ah; j++) {
            lui_rect l;
            l.x = ax + j; l.y = ay + j; l.w = aw - 2 * j; l.h = 1;
            if (l.w > 0) lui_fill(u, l, LUI_ACCENT);
        }
    }
    focus_ring(u, r, f);
    return cur;
}

bool lui_toggle(lui *u, lui_rect r, bool v, const char *yes, const char *no)
{
    bool f, hot;
    int px = text_px(u), half = r.w / 2, i;
    control(u, r, &f, &hot);
    if (take_key(u, f, LK_LEFT)) v = true;
    if (take_key(u, f, LK_RIGHT)) v = false;
    if (take_key(u, f, LK_ENTER)) v = !v;
    if (hot && u->mpressed) v = u->mx < r.x + half;
    for (i = 0; i < 2; i++) {
        lui_rect b;
        bool on = (i == 0) == v;
        const char *t = i == 0 ? yes : no;
        int tw = lt_width(px, t);
        int dot = lui_px(u, 12);
        b.x = r.x + i * half; b.y = r.y; b.w = half - lui_px(u, 4); b.h = r.h;
        lui_fill(u, b, on ? 0xFF3C3530u : LUI_FIELD);
        {
            lui_rect c;
            c.x = b.x + lui_px(u, 8); c.y = b.y + (b.h - dot) / 2; c.w = dot; c.h = dot;
            lui_frame(u, c, on ? LUI_ACCENT : LUI_DIM, 2);
            if (on) { c.x += 3; c.y += 3; c.w -= 6; c.h -= 6; lui_fill(u, c, LUI_ACCENT); }
        }
        lt_draw(&u->s, b.x + lui_px(u, 28), b.y + (b.h - lt_line_height(px)) / 2 + 1, px, on ? LUI_TEXT : LUI_DIM, t);
        (void)tw;
    }
    focus_ring(u, r, f);
    return v;
}

float lui_slider(lui *u, lui_rect r, float v, float lo, float hi, float step, const char *vt)
{
    bool f, hot;
    int id = control(u, r, &f, &hot);
    int px = text_px(u), vw = lui_px(u, 64);
    lui_rect track;
    float t;
    track.x = r.x + lui_px(u, 6); track.w = r.w - vw - lui_px(u, 16);
    track.h = lui_px(u, 6); track.y = r.y + (r.h - track.h) / 2;
    if (take_key(u, f, LK_LEFT)) v -= step;
    if (take_key(u, f, LK_RIGHT)) v += step;
    if (hot && u->mpressed) u->drag_id = id;
    if (u->drag_id == id) {
        if (!u->mdown) u->drag_id = -1;
        else {
            float k = (float)(u->mx - track.x) / (float)(track.w > 1 ? track.w : 1);
            if (k < 0) k = 0;
            if (k > 1) k = 1;
            v = lo + k * (hi - lo);
            v = lo + (float)(int)((v - lo) / step + 0.5f) * step;
        }
    }
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    t = (v - lo) / (hi - lo);
    lui_fill(u, r, hot || f ? LUI_FIELD_HOT : LUI_FIELD);
    lui_fill(u, track, 0xFF5A524Cu);
    {
        lui_rect fill = track, knob;
        fill.w = (int)(track.w * t);
        lui_fill(u, fill, LUI_ACCENT);
        knob.w = lui_px(u, 10); knob.h = lui_px(u, 18);
        knob.x = track.x + fill.w - knob.w / 2; knob.y = r.y + (r.h - knob.h) / 2;
        lui_fill(u, knob, LUI_TEXT);
    }
    lt_draw(&u->s, r.x + r.w - vw, r.y + (r.h - lt_line_height(px)) / 2 + 1, px, LUI_TEXT, vt);
    focus_ring(u, r, f);
    return v;
}

bool lui_button(lui *u, lui_rect r, const char *text, bool primary)
{
    bool f, hot, hit;
    int px = lui_px(u, primary ? 17 : 15), tw = lt_width(px, text);
    control(u, r, &f, &hot);
    hit = (hot && u->mreleased) || take_key(u, f, LK_ENTER);
    lui_fill(u, r, primary ? (hot || f ? 0xFF30D8FFu : LUI_ACCENT) : (hot || f ? LUI_FIELD_HOT : LUI_FIELD));
    lui_frame(u, r, primary ? 0xFF0890C0u : 0xFF5A524Cu, 1);
    lt_draw(&u->s, r.x + (r.w - tw) / 2, r.y + (r.h - lt_line_height(px)) / 2 + 1, px, primary ? 0xFF101010u : LUI_TEXT, text);
    focus_ring(u, r, f);
    return hit;
}

bool lui_cell(lui *u, lui_rect r, const char *text, bool highlight)
{
    bool f, hot, hit;
    int px = text_px(u);
    control(u, r, &f, &hot);
    hit = (hot && u->mreleased) || take_key(u, f, LK_ENTER);
    lui_fill(u, r, highlight ? 0xFF205A20u : hot || f ? LUI_FIELD_HOT : LUI_FIELD);
    lui_text_fit(u, r.x + lui_px(u, 8), r.y + (r.h - lt_line_height(px)) / 2 + 1, px, LUI_TEXT, text, r.w - lui_px(u, 12));
    focus_ring(u, r, f);
    return hit;
}

bool lui_tab(lui *u, lui_rect r, const char *text, bool selected)
{
    int px = lui_px(u, 16), tw = lt_width(px, text);
    bool hot = lui_hover(u, r);
    lui_fill(u, r, selected ? LUI_PANEL : hot ? 0xFF2F2A26u : LUI_BG);
    if (selected) { lui_rect b = r; b.h = lui_px(u, 3); lui_fill(u, b, LUI_ACCENT); }
    lt_draw(&u->s, r.x + (r.w - tw) / 2, r.y + (r.h - lt_line_height(px)) / 2 + 1, px, selected ? LUI_TEXT : LUI_DIM, text);
    return hot && u->mreleased;
}

/* ------------------------------------------------------------ popup list */

static void popup(lui *u)
{
    int px = text_px(u), ih = lui_px(u, 28), vis, i, maxh;
    lui_rect a = u->popup_anchor, box;
    maxh = u->s.h - a.y - a.h - lui_px(u, 8);
    if (maxh < ih * 4) maxh = a.y - lui_px(u, 8);
    vis = u->popup_n;
    if (vis * ih > maxh) vis = maxh / ih;
    if (vis < 1) vis = 1;
    box.x = a.x; box.w = a.w; box.h = vis * ih;
    box.y = (a.y + a.h + box.h <= u->s.h) ? a.y + a.h : a.y - box.h;
    /* keys */
    switch (u->key) {
    case LK_UP: if (u->popup_sel > 0) u->popup_sel--; break;
    case LK_DOWN: if (u->popup_sel + 1 < u->popup_n) u->popup_sel++; break;
    case LK_HOME: u->popup_sel = 0; break;
    case LK_END: u->popup_sel = u->popup_n - 1; break;
    case LK_ENTER: u->popup_result = u->popup_sel; u->popup_result_id = u->popup_id; u->popup_id = -1; return;
    case LK_BACK: case LK_LEFT: case LK_RIGHT: u->popup_id = -1; return;
    default: break;
    }
    if (u->wheel) u->popup_scroll -= u->wheel;
    if (u->popup_sel < u->popup_scroll) u->popup_scroll = u->popup_sel;
    if (u->popup_sel >= u->popup_scroll + vis) u->popup_scroll = u->popup_sel - vis + 1;
    if (u->popup_scroll > u->popup_n - vis) u->popup_scroll = u->popup_n - vis;
    if (u->popup_scroll < 0) u->popup_scroll = 0;
    /* mouse */
    if (u->mx >= box.x && u->mx < box.x + box.w && u->my >= box.y && u->my < box.y + box.h) {
        int k = u->popup_scroll + (u->my - box.y) / ih;
        if (k < u->popup_n) {
            u->popup_sel = k;
            if (u->mreleased) { u->popup_result = k; u->popup_result_id = u->popup_id; u->popup_id = -1; return; }
        }
    } else if (u->mpressed) {
        u->popup_id = -1;
        return;
    }
    {
        lui_rect sh = box;
        sh.x += 4; sh.y += 4;
        lui_fill(u, sh, 0x80000000u);
    }
    lui_fill(u, box, 0xFF332D29u);
    for (i = 0; i < vis && u->popup_scroll + i < u->popup_n; i++) {
        int k = u->popup_scroll + i;
        lui_rect it;
        it.x = box.x; it.y = box.y + i * ih; it.w = box.w; it.h = ih;
        if (k == u->popup_sel) lui_fill(u, it, 0xFF4E443Cu);
        lui_text_fit(u, it.x + lui_px(u, 8), it.y + (ih - lt_line_height(px)) / 2 + 1, px, k == u->popup_sel ? LUI_ACCENT : LUI_TEXT,
                     u->popup_text[k], it.w - lui_px(u, 16));
    }
    if (vis < u->popup_n) {
        lui_rect sb;
        sb.w = lui_px(u, 4); sb.x = box.x + box.w - sb.w - 1;
        sb.h = box.h * vis / u->popup_n; sb.y = box.y + (box.h - sb.h) * u->popup_scroll / (u->popup_n - vis);
        lui_fill(u, sb, LUI_DIM);
    }
    lui_frame(u, box, LUI_ACCENT, 1);
}

void lui_end(lui *u)
{
    if (u->popup_id >= 0) {
        popup(u);
        u->key_used = true;
    }
    if (!u->key_used && u->next_id > 0) {
        if (u->key == LK_DOWN) { u->focus = u->focus < 0 ? 0 : (u->focus + 1) % u->next_id; u->focus_visible = true; }
        else if (u->key == LK_UP) { u->focus = u->focus <= 0 ? u->next_id - 1 : u->focus - 1; u->focus_visible = true; }
        else if (u->key == LK_HOME) { u->focus = 0; u->focus_visible = true; }
        else if (u->key == LK_END) { u->focus = u->next_id - 1; u->focus_visible = true; }
        else if (u->key == LK_LEFT || u->key == LK_RIGHT || u->key == LK_ENTER) {
            if (u->focus < 0) { u->focus = 0; u->focus_visible = true; }
        }
    }
    if (u->key == LK_UP || u->key == LK_DOWN || u->key == LK_LEFT || u->key == LK_RIGHT || u->key == LK_ENTER) u->focus_visible = true;
    u->count_last = u->next_id;
    u->key = LK_NONE;
    u->key_used = false;
    u->mpressed = u->mreleased = false;
    u->wheel = 0;
}
