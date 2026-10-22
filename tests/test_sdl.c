#include "../src/platform/nr_sdl.h"
#include "../src/input/input.h"
#include <stdio.h>
#include <string.h>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, SDL_GetError()); return 1; } } while (0)
int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_Event event, got;
    nr_sdl_audio audio;
    u32 pixels[64 * 48];
    s16 silence[512 * 2] = {0};
    int w = 0, h = 0, pitch, x, y, found = 0, count, pick = -1;
    void *dst;
    nr_input_device devices[NR_MAX_INPUT_DEVICES];
    nr_pad *pad;
    SDL_Joystick *joystick;
#ifdef NR_SDL3
    SDL_JoystickID device;
    SDL_VirtualJoystickDesc desc;
#else
    int device;
#endif
    (void)argc; (void)argv;
    REQUIRE(nr_sdl_init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
    win = nr_sdl_window("NR SDL test", 64, 48, false, false);
    REQUIRE(win);
    ren = nr_sdl_renderer(win, true, false);
    REQUIRE(ren && nr_sdl_output_size(ren, &w, &h) == 0 && w == 64 && h == 48);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 4, 4);
    REQUIRE(tex && nr_sdl_lock_texture(tex, &dst, &pitch));
    for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) ((u32 *)((u8 *)dst + (size_t)y * (size_t)pitch))[x] = 0xff0000ffu;
    SDL_UnlockTexture(tex);
    SDL_RenderClear(ren);
    nr_sdl_render_texture(ren, tex, NULL, NULL);
    REQUIRE(nr_sdl_read_pixels(ren, pixels, w, h));
    REQUIRE(pixels[0] == 0xff0000ffu && pixels[64 * 48 - 1] == 0xff0000ffu);
    SDL_RenderPresent(ren);
    SDL_zero(event);
    event.type = SDL_EVENT_KEY_DOWN;
    NR_SDL_KEY(event) = SDLK_F11;
    NR_SDL_SCAN(event) = SDL_SCANCODE_F11;
    SDL_PushEvent(&event);
    while (SDL_PollEvent(&got)) if (got.type == SDL_EVENT_KEY_DOWN && NR_SDL_KEY(got) == SDLK_F11 && NR_SDL_SCAN(got) == SDL_SCANCODE_F11) found++;
    REQUIRE(found == 1);
    audio = nr_sdl_audio_open();
    REQUIRE(audio);
    REQUIRE(nr_sdl_audio_queue(audio, silence, sizeof(silence)));
    REQUIRE(nr_sdl_audio_queued(audio) == sizeof(silence));
    nr_sdl_audio_clear(audio);
    REQUIRE(nr_sdl_audio_queued(audio) == 0);
    nr_sdl_audio_pause(audio, false);
    nr_sdl_audio_pause(audio, true);
    nr_sdl_audio_close(audio);
#ifdef NR_SDL3
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6; desc.nbuttons = 15; desc.name = "NR virtual pad";
    device = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(device != 0);
#else
    device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
    REQUIRE(device >= 0);
#endif
    count = nr_input_list(devices, NR_MAX_INPUT_DEVICES);
    for (x = 0; x < count; x++) if (devices[x].kind == NR_DEV_SDL && devices[x].sdl_index == (u32)device) pick = x;
    REQUIRE(pick >= 0);
    pad = nr_pad_open(devices[pick].id, 0);
    REQUIRE(pad);
    joystick = SDL_OpenJoystick(device);
    REQUIRE(joystick);
#ifdef NR_SDL3
    REQUIRE(SDL_SetJoystickVirtualButton(joystick, 0, true));
    SDL_UpdateJoysticks();
#else
    REQUIRE(SDL_JoystickSetVirtualButton(joystick, 0, 1) == 0);
    SDL_JoystickUpdate();
#endif
    REQUIRE(nr_pad_binding_active(pad, "b0"));
    REQUIRE(nr_sdl_button_from_string("a") == SDL_GAMEPAD_BUTTON_SOUTH);
    REQUIRE(nr_sdl_button_from_string("b") == SDL_GAMEPAD_BUTTON_EAST);
    REQUIRE(strcmp(nr_sdl_button_name(SDL_GAMEPAD_BUTTON_SOUTH), "a") == 0);
    nr_pad_close(pad);
    SDL_CloseJoystick(joystick);
#ifdef NR_SDL3
    REQUIRE(SDL_DetachVirtualJoystick(device));
#else
    REQUIRE(SDL_JoystickDetachVirtual(device) == 0);
#endif
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    puts("SDL adapter: video, pixels, events, audio and virtual joystick passed");
    return 0;
}
