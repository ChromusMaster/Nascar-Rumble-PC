#ifndef NR_SDL_H
#define NR_SDL_H
#include "../common/common.h"
#ifdef NR_SDL3
#define SDL_DISABLE_OLD_NAMES
#include <SDL3/SDL.h>
#define NR_SDL_KEY(e) ((e).key.key)
#define NR_SDL_SCAN(e) ((e).key.scancode)
#define NR_SDL_MOD(e) ((e).key.mod)
#define NR_SDL_PAD_BUTTON(e) ((e).gbutton.button)
typedef SDL_AudioStream *nr_sdl_audio;
#else
#include <SDL.h>
#include <SDL_syswm.h>
#define NR_SDL_KEY(e) ((e).key.keysym.sym)
#define NR_SDL_SCAN(e) ((e).key.keysym.scancode)
#define NR_SDL_MOD(e) ((e).key.keysym.mod)
#define NR_SDL_PAD_BUTTON(e) ((e).cbutton.button)
typedef SDL_AudioDeviceID nr_sdl_audio;
#define SDL_KMOD_SHIFT KMOD_SHIFT
#define SDL_EVENT_GAMEPAD_BUTTON_DOWN SDL_CONTROLLERBUTTONDOWN
#define SDL_GAMEPAD_AXIS_INVALID SDL_CONTROLLER_AXIS_INVALID
#define SDL_GAMEPAD_AXIS_COUNT SDL_CONTROLLER_AXIS_MAX
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERRIGHT
#define SDL_GAMEPAD_BUTTON_SOUTH SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_EAST SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define SDL_GAMEPAD_BUTTON_DPAD_UP SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_INVALID SDL_CONTROLLER_BUTTON_INVALID
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_COUNT SDL_CONTROLLER_BUTTON_MAX
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_START SDL_CONTROLLER_BUTTON_START
#define SDL_GL_DestroyContext SDL_GL_DeleteContext
#define SDL_Gamepad SDL_GameController
#define SDL_GamepadAxis SDL_GameControllerAxis
#define SDL_GamepadButton SDL_GameControllerButton
#define SDL_CloseGamepad SDL_GameControllerClose
#define SDL_GetGamepadAxis SDL_GameControllerGetAxis
#define SDL_GetGamepadAxisFromString SDL_GameControllerGetAxisFromString
#define SDL_GetGamepadButton SDL_GameControllerGetButton
#define SDL_GetGamepadButtonFromString SDL_GameControllerGetButtonFromString
#define SDL_GetGamepadJoystick SDL_GameControllerGetJoystick
#define SDL_GetGamepadStringForAxis SDL_GameControllerGetStringForAxis
#define SDL_GetGamepadStringForButton SDL_GameControllerGetStringForButton
#define SDL_OpenGamepad SDL_GameControllerOpen
#define SDL_GetCurrentRenderOutputSize SDL_GetRendererOutputSize
#define SDL_INIT_GAMEPAD SDL_INIT_GAMECONTROLLER
#define SDL_IsGamepad SDL_IsGameController
#define SDL_EVENT_JOYSTICK_ADDED SDL_JOYDEVICEADDED
#define SDL_EVENT_JOYSTICK_REMOVED SDL_JOYDEVICEREMOVED
#define SDL_CloseJoystick SDL_JoystickClose
#define SDL_GUID SDL_JoystickGUID
#define SDL_JoystickConnected SDL_JoystickGetAttached
#define SDL_GetJoystickAxis SDL_JoystickGetAxis
#define SDL_GetJoystickButton SDL_JoystickGetButton
#define SDL_GetJoystickHat SDL_JoystickGetHat
#define SDL_GetJoystickID SDL_JoystickInstanceID
#define SDL_GetNumJoystickAxes SDL_JoystickNumAxes
#define SDL_GetNumJoystickButtons SDL_JoystickNumButtons
#define SDL_GetNumJoystickHats SDL_JoystickNumHats
#define SDL_OpenJoystick SDL_JoystickOpen
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_KEY_UP SDL_KEYUP
#define SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_WHEEL SDL_MOUSEWHEEL
#define SDL_SCANCODE_COUNT SDL_NUM_SCANCODES
#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_RenderTexture SDL_RenderCopy
#define SDL_SetRenderVSync SDL_RenderSetVSync
#define SDL_SCALEMODE_LINEAR SDL_ScaleModeLinear
#define SDL_SCALEMODE_NEAREST SDL_ScaleModeNearest
#define SDL_WINDOW_HIGH_PIXEL_DENSITY SDL_WINDOW_ALLOW_HIGHDPI
#endif

static inline SDL_GamepadButton nr_sdl_button_from_string(const char *name)
{
#ifdef NR_SDL3
    if (SDL_strcasecmp(name, "a") == 0) return SDL_GAMEPAD_BUTTON_SOUTH;
    if (SDL_strcasecmp(name, "b") == 0) return SDL_GAMEPAD_BUTTON_EAST;
    if (SDL_strcasecmp(name, "x") == 0) return SDL_GAMEPAD_BUTTON_WEST;
    if (SDL_strcasecmp(name, "y") == 0) return SDL_GAMEPAD_BUTTON_NORTH;
#endif
    return SDL_GetGamepadButtonFromString(name);
}
static inline const char *nr_sdl_button_name(SDL_GamepadButton button)
{
#ifdef NR_SDL3
    if (button == SDL_GAMEPAD_BUTTON_SOUTH) return "a";
    if (button == SDL_GAMEPAD_BUTTON_EAST) return "b";
    if (button == SDL_GAMEPAD_BUTTON_WEST) return "x";
    if (button == SDL_GAMEPAD_BUTTON_NORTH) return "y";
#endif
    return SDL_GetGamepadStringForButton(button);
}
static inline void nr_sdl_gl_size(SDL_Window *w, int *x, int *y)
{
#ifdef NR_SDL3
    SDL_GetWindowSizeInPixels(w, x, y);
#else
    SDL_GL_GetDrawableSize(w, x, y);
#endif
}
static inline u32 nr_sdl_ticks(void) { return (u32)SDL_GetTicks(); }
static inline bool nr_sdl_init(Uint32 flags)
{
#ifdef NR_SDL3
    return SDL_InitSubSystem(flags);
#else
    return SDL_InitSubSystem(flags) == 0;
#endif
}
static inline SDL_Window *nr_sdl_window(const char *title, int w, int h, bool gl, bool fullscreen)
{
#ifdef NR_SDL3
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *win;
    if (gl) flags |= SDL_WINDOW_OPENGL;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    win = SDL_CreateWindow(title, w, h, flags);
    if (win && !fullscreen) SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return win;
#else
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (gl) flags |= SDL_WINDOW_OPENGL;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
#endif
}
static inline void nr_sdl_toggle_fullscreen(SDL_Window *w)
{
#ifdef NR_SDL3
    SDL_SetWindowFullscreen(w, !(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN));
#else
    SDL_SetWindowFullscreen(w, (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}
static inline void *nr_sdl_native_window(SDL_Window *w)
{
#ifdef _WIN32
#ifdef NR_SDL3
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    return SDL_GetWindowWMInfo(w, &info) ? info.info.win.window : NULL;
#endif
#else
    (void)w;
    return NULL;
#endif
}
static inline SDL_Renderer *nr_sdl_renderer(SDL_Window *w, bool software, bool vsync)
{
#ifdef NR_SDL3
    SDL_Renderer *r = SDL_CreateRenderer(w, software ? "software" : NULL);
    if (r) SDL_SetRenderVSync(r, vsync ? 1 : 0);
    return r;
#else
    return SDL_CreateRenderer(w, -1, (software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED) | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
#endif
}
static inline const char *nr_sdl_renderer_name(SDL_Renderer *r)
{
#ifdef NR_SDL3
    const char *name = SDL_GetRendererName(r);
    return name ? name : "unknown";
#else
    SDL_RendererInfo info;
    return SDL_GetRendererInfo(r, &info) == 0 ? info.name : "unknown";
#endif
}
static inline int nr_sdl_output_size(SDL_Renderer *r, int *w, int *h)
{
#ifdef NR_SDL3
    return SDL_GetCurrentRenderOutputSize(r, w, h) ? 0 : -1;
#else
    return SDL_GetRendererOutputSize(r, w, h);
#endif
}
static inline bool nr_sdl_lock_texture(SDL_Texture *t, void **dst, int *pitch)
{
#ifdef NR_SDL3
    return SDL_LockTexture(t, NULL, dst, pitch);
#else
    return SDL_LockTexture(t, NULL, dst, pitch) == 0;
#endif
}
static inline void nr_sdl_render_texture(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst)
{
#ifdef NR_SDL3
    SDL_FRect a, b;
    if (src) { a.x = (float)src->x; a.y = (float)src->y; a.w = (float)src->w; a.h = (float)src->h; }
    if (dst) { b.x = (float)dst->x; b.y = (float)dst->y; b.w = (float)dst->w; b.h = (float)dst->h; }
    SDL_RenderTexture(r, t, src ? &a : NULL, dst ? &b : NULL);
#else
    SDL_RenderCopy(r, t, src, dst);
#endif
}
static inline bool nr_sdl_read_pixels(SDL_Renderer *r, u32 *pixels, int w, int h)
{
#ifdef NR_SDL3
    SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
    bool ok;
    if (!s) return false;
    ok = s->w == w && s->h == h && SDL_ConvertPixels(w, h, s->format, s->pixels, s->pitch, SDL_PIXELFORMAT_ABGR8888, pixels, w * 4);
    SDL_DestroySurface(s);
    return ok;
#else
    (void)h;
    return SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ABGR8888, pixels, w * 4) == 0;
#endif
}
static inline bool nr_sdl_display_bounds(SDL_Rect *rect)
{
#ifdef NR_SDL3
    return SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), rect);
#else
    return SDL_GetDisplayUsableBounds(0, rect) == 0;
#endif
}
static inline Uint32 nr_sdl_mouse(int *x, int *y)
{
#ifdef NR_SDL3
    float fx, fy;
    Uint32 buttons = SDL_GetMouseState(&fx, &fy);
    *x = (int)fx; *y = (int)fy;
    return buttons;
#else
    return SDL_GetMouseState(x, y);
#endif
}
static inline void nr_sdl_keyboard(u8 *out, size_t cap)
{
    int count = 0;
    size_t i;
#ifdef NR_SDL3
    const bool *keys = SDL_GetKeyboardState(&count);
#else
    const Uint8 *keys = SDL_GetKeyboardState(&count);
#endif
    for (i = 0; i < cap; i++) out[i] = keys && i < (size_t)count && keys[i];
}
static inline nr_sdl_audio nr_sdl_audio_open(void)
{
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 44100; want.channels = 2;
#ifdef NR_SDL3
    want.format = SDL_AUDIO_S16;
    return SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
#else
    want.format = AUDIO_S16SYS; want.samples = 512;
    return SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
#endif
}
static inline void nr_sdl_audio_pause(nr_sdl_audio a, bool pause)
{
#ifdef NR_SDL3
    SDL_AudioDeviceID id = SDL_GetAudioStreamDevice(a);
    if (pause) SDL_PauseAudioDevice(id); else SDL_ResumeAudioDevice(id);
#else
    SDL_PauseAudioDevice(a, pause ? 1 : 0);
#endif
}
static inline void nr_sdl_audio_clear(nr_sdl_audio a)
{
#ifdef NR_SDL3
    SDL_ClearAudioStream(a);
#else
    SDL_ClearQueuedAudio(a);
#endif
}
static inline u32 nr_sdl_audio_queued(nr_sdl_audio a)
{
#ifdef NR_SDL3
    int n = SDL_GetAudioStreamQueued(a);
    return n > 0 ? (u32)n : 0;
#else
    return SDL_GetQueuedAudioSize(a);
#endif
}
static inline bool nr_sdl_audio_queue(nr_sdl_audio a, const void *p, u32 bytes)
{
#ifdef NR_SDL3
    if (bytes > 0x7fffffffu) return false;
    return SDL_PutAudioStreamData(a, p, (int)bytes);
#else
    return SDL_QueueAudio(a, p, bytes) == 0;
#endif
}
static inline void nr_sdl_audio_close(nr_sdl_audio a)
{
#ifdef NR_SDL3
    SDL_DestroyAudioStream(a);
#else
    SDL_CloseAudioDevice(a);
#endif
}
#endif
