/* Steady physics step ("engine.physics_step").
 *
 * NASCAR Rumble measures the time of every game frame in ticks of 1/300 s
 * (five ticks per NTSC field), averages the last four values and passes the
 * result (clamped to 25 ticks) to the vehicle, collision and camera code.
 * On the console the frame time keeps changing between 5 and 10+ ticks as
 * the scene gets busier, and the simulation is not fully independent of the
 * step size: integration error and collision tests change with the step, so
 * cars feel heavier or lighter and collision probes can skip thin surfaces
 * when a long step happens.
 *
 * This optional enhancement fixes the step: the frame function (800269C0)
 * waits until at least N ticks have passed and then the game receives
 * exactly N ticks. The game itself already supports this behaviour: its
 * replay playback path waits for a recorded step in the same way. No game
 * data or code is modified: two observation hooks change registers/RAM at
 * run time only, after checking the original instructions.
 *
 *   800269E0  blez v0,800269D0   v0 = ticks elapsed since the last frame
 *   80026CF8  lw v0,1636(gp)     the averaged step is final at gp+1532
 *
 * If a frame takes longer than N ticks (CPU too slow for the chosen step)
 * the game slows down, exactly like console slowdown, instead of taking a
 * longer step. Use the CPU overclock option with the 60 Hz step. */
#include "../app/app.h"

static u32 g_step;          /* 0 = original behaviour */
static bool g_normal_path;  /* this frame went through the measured wait */

#define PACE_GP_STEP 1532

static bool hook_wait(psx *p, u64 *cycles)
{
    s32 elapsed = (s32)p->cpu.r[2];
    (void)cycles;
    if (!g_step) return false;
    if (elapsed > 0 && elapsed < (s32)g_step) p->cpu.r[2] = 0;   /* keep waiting */
    else if (elapsed > 0) g_normal_path = true;
    return false;
}

static bool hook_step(psx *p, u64 *cycles)
{
    (void)cycles;
    if (!g_step || !g_normal_path) return false;
    g_normal_path = false;
    psx_ram_w32(p, p->cpu.r[28] + PACE_GP_STEP, g_step);
    return false;
}

void nr_pace_register(psx *p, int step_ticks)
{
    g_step = 0;
    g_normal_path = false;
    if (step_ticks <= 0) return;
    if (psx_ram_r32(p, 0x800269E0u) != 0x1840FFFBu || psx_ram_r32(p, 0x800269DCu) != 0x00431023u ||
        psx_ram_r32(p, 0x80026CF8u) != 0x8F820664u || psx_ram_r32(p, 0x80026CFCu) != 0x8F8405FCu) {
        NR_WARN("pace", "original code does not match the reference executable; steady physics step not enabled");
        return;
    }
    g_step = (u32)step_ticks;
    psx_override_add(p, 0x800269E0u, hook_wait, "Frame wait (steady physics step)", true);
    psx_override_add(p, 0x80026CF8u, hook_step, "Frame step (steady physics step)", true);
    NR_INFO("pace", "steady physics step: %u ticks (%.0f Hz)", g_step, 300.0 / g_step);
}
