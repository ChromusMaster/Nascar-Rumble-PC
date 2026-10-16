/* PlayStation execution core used to run the original NASCAR Rumble program
 * logic (supplied by the user) on a modern machine. Every hardware block is
 * reimplemented from public documentation; no Sony/EA code is included. */
#ifndef NR_PSX_H
#define NR_PSX_H

#include "../common/common.h"
#include "../disc/disc.h"

#define PSX_CPU_HZ        33868800u
#define PSX_RAM_SIZE      (2u * 1024u * 1024u)
#define PSX_SCRATCH_SIZE  1024u
#define PSX_BIOS_SIZE     (512u * 1024u)
#define PSX_VRAM_W        1024
#define PSX_VRAM_H        512
#define PSX_SPU_RAM_SIZE  (512u * 1024u)

/* Clocks. CPU 33.8688 MHz; NTSC consoles use a 53.693175 MHz video clock
 * (psx-spx "GPU Timings"). Video cycles -> CPU cycles = v * NUM / DEN. */
#define PSX_CPU_HZ 33868800u
#define PSX_VCLK_TO_CPU_NUM 33868800ull
#define PSX_VCLK_TO_CPU_DEN 53693175ull

/* IRQ lines (I_STAT bits) */
enum {
    IRQ_VBLANK = 0, IRQ_GPU = 1, IRQ_CDROM = 2, IRQ_DMA = 3,
    IRQ_TMR0 = 4, IRQ_TMR1 = 5, IRQ_TMR2 = 6, IRQ_SIO0 = 7,
    IRQ_SIO1 = 8, IRQ_SPU = 9, IRQ_PIO = 10
};

/* Scheduler event slots */
enum {
    EV_GPU_LINE = 0,   /* scanline / vblank timing */
    EV_TIMER0, EV_TIMER1, EV_TIMER2,
    EV_CDROM,          /* async CD-ROM command/response/sector */
    EV_CDROM_READ,     /* sector delivery */
    EV_DMA,            /* DMA completion IRQ */
    EV_SPU,            /* SPU sample generation */
    EV_SIO,            /* SIO0 transfer completion */
    EV_COUNT
};

/* ---------------------------------------------------------------- CPU --- */
typedef struct {
    u32 r[32];
    u32 hi, lo;
    u32 pc;          /* address of the instruction about to execute */
    u32 next_pc;
    u32 cur_pc;      /* address of the instruction being executed */
    u32 pend_reg, pend_val;  /* load-delay slot */
    bool branch;     /* current instruction is a taken/untaken branch */
    bool in_delay;   /* current instruction is in a branch delay slot */
    u32 cop0[32];
    u64 muldiv_ready; /* cycle when HI/LO become available */
    u32 ic_tag[256];  /* instruction cache tags (timing model only) */
    u8 ic_valid[256];
    u32 last_instr;
} psx_cpu;

#define C0_BPC 3
#define C0_BDA 5
#define C0_JUMPDEST 6
#define C0_DCIC 7
#define C0_BADVADDR 8
#define C0_BDAM 9
#define C0_BPCM 11
#define C0_SR 12
#define C0_CAUSE 13
#define C0_EPC 14
#define C0_PRID 15

enum {
    EXC_INT = 0, EXC_ADEL = 4, EXC_ADES = 5, EXC_IBE = 6, EXC_DBE = 7,
    EXC_SYS = 8, EXC_BP = 9, EXC_RI = 10, EXC_CPU = 11, EXC_OV = 12
};

/* ---------------------------------------------------------------- GTE --- */
typedef struct {
    u32 d[32];   /* data registers */
    u32 c[32];   /* control registers */
    /* enhancements (off = 0/NULL): */
    struct nr_pgxp_s *pgxp;     /* sub-pixel vertex cache (render only) */
    s32 ws_num, ws_den;         /* widescreen: horizontal projection scale (changes game-visible results) */
} psx_gte;

/* ---------------------------------------------------------------- DMA --- */
typedef struct {
    u32 madr[7], bcr[7], chcr[7];
    u32 dpcr, dicr;
    u32 pending_done;   /* channels whose completion IRQ is pending */
    u32 active;         /* channels with a transfer in flight */
} psx_dma;

/* -------------------------------------------------------------- Timers --- */
typedef struct {
    u32 counter;   /* current value (16-bit) */
    u32 mode;
    u32 target;
    u64 last_update;   /* cycle of last counter sync */
    u32 frac;          /* fractional ticks for divided clocks */
    bool paused;
    bool irq_done;     /* one-shot IRQ already fired */
} psx_timer;

/* ----------------------------------------------------------------- GPU --- */
typedef struct {
    u16 *vram;          /* 1024x512 15-bit */
    u32 stat;
    u32 gpuread;
    /* drawing state */
    s32 draw_x1, draw_y1, draw_x2, draw_y2;
    s32 off_x, off_y;
    u32 tex_window;     /* raw GP0(E2) */
    u32 texpage;        /* raw texpage bits (GP0 E1 / polygon) */
    bool tex_disable;
    bool rect_flip_x, rect_flip_y;
    bool set_mask, check_mask;
    /* display */
    u32 disp_x, disp_y;
    u32 hrange1, hrange2, vrange1, vrange2;
    u32 disp_mode;      /* GP1(08) */
    bool disp_enabled;
    /* command FIFO / state machine */
    u32 cmd[16];
    int cmd_len, cmd_need;
    int state;          /* 0 = commands, 1 = CPU->VRAM, 2 = VRAM->CPU, 3 = polyline */
    u32 xfer_x, xfer_y, xfer_w, xfer_h, xfer_cx, xfer_cy;
    /* video timing */
    u32 line;           /* current scanline */
    bool in_vblank;
    bool interlace_field;
    u64 frame_count;
    u64 prims;
    u64 flips;          /* display start changes (game frames presented) */
    bool frame_ready;   /* set at vblank start */
    u32 dma_dir;
    u64 line_frac;
    /* drawing time model (see gpu.c) */
    bool timing;            /* false = instant rendering */
    u64 busy_until;         /* CPU cycle when the current drawing finishes */
    u32 prim_pixels;        /* pixels touched by the primitive being drawn */
    struct nr_rlist *rec;   /* optional render command recorder (enhanced renderers) */
    struct nr_pgxp_s *pgxp; /* sub-pixel vertex cache (shared with the GTE) */
} psx_gpu;

/* --------------------------------------------------------------- CDROM --- */
typedef struct {
    u8 index;
    u8 param[16]; int nparam;
    u8 resp[16];  int nresp, resp_pos;
    u8 resp_pending_irq;          /* queued INT for current response */
    u8 irq_enable, irq_flags;
    u8 stat;                       /* drive status byte */
    u8 mode;
    u8 cmd;                        /* command being processed */
    int cmd_stage;
    bool busy;
    /* second response queued (e.g. INT2 after INT3) */
    u8 resp2[16]; int nresp2; u8 resp2_irq; u64 resp2_delay;
    bool resp2_valid;
    /* position */
    u32 setloc_lba; bool setloc_pending;
    u32 cur_lba;
    bool reading, playing, seeking;
    bool motor_on;
    bool shell_open;
    /* data FIFO */
    u8 sector[2352];               /* last read sector (raw) */
    u8 data[2352]; int data_len, data_pos;
    bool data_ready_waiting;       /* sector read but INT1 not acknowledged yet */
    u8 pending_sector[2352]; bool has_pending_sector;
    u8 req;                        /* request register */
    u8 filter_file, filter_chan;
    u8 last_header[4], last_subheader[4];
    /* audio volumes */
    u8 vol_ll, vol_lr, vol_rl, vol_rr;
    u8 vol_pend[4];
    bool muted, adpcm_muted;
    u64 read_period;               /* cycles per sector */
    u32 sectors_read;
} psx_cdrom;

/* ----------------------------------------------------------------- SPU --- */
typedef struct {
    /* registers */
    s16 vol_l, vol_r;
    u16 pitch;
    u16 start_addr;     /* *8 */
    u16 adsr_lo, adsr_hi;
    u16 repeat_addr;
    /* state */
    u32 cur_addr;       /* byte address */
    u32 repeat;         /* byte address */
    u32 counter;        /* pitch counter (12-bit fraction) */
    s16 hist[2];
    s16 samples[28];
    s16 prev_samples[4];  /* for interpolation */
    int block_decoded;
    int adsr_phase;     /* 0 off,1 attack,2 decay,3 sustain,4 release */
    s32 adsr_vol;
    s32 adsr_counter;
    bool key_on, ended;
    s16 last_out;
    bool ignore_loop_addr_write;
} psx_spu_voice;

typedef struct {
    u8 *ram;
    psx_spu_voice v[24];
    u16 regs[0x200];     /* raw register mirror for 1F801C00..1F801FFF */
    u16 ctrl, stat;
    u32 transfer_addr;   /* byte address */
    u16 irq_addr;
    u32 endx;
    u32 kon_pending, koff_pending;
    s16 main_vol_l, main_vol_r;
    s16 rev_vol_l, rev_vol_r;
    s16 cd_vol_l, cd_vol_r, ext_vol_l, ext_vol_r;
    u32 noise_level; s32 noise_timer;
    u32 reverb_cur;      /* current reverb buffer address (byte) */
    bool reverb_phase;
    s16 rev_in_l, rev_in_r;
    s16 rev_out_l, rev_out_r;
    /* output ring buffer (stereo s16 @ 44100) */
    s16 *out; u32 out_cap, out_w, out_r;
    u32 capture_pos;
    s16 cd_l, cd_r;
    void *ext;          /* internal envelope/reverb state (spu.c) */
} psx_spu;

/* ---------------------------------------------------------------- MDEC --- */
typedef struct {
    u32 status;
    u32 cmd;
    int state;              /* 0 idle, 1 decode, 2 quant table, 3 scale table */
    u32 words_left;
    u16 *in; u32 in_len, in_cap;   /* input halfwords for decoding */
    u32 *out; u32 out_len, out_pos, out_cap;
    u8 quant_y[64], quant_uv[64];
    s16 scale[64];
    bool dma_in_enabled, dma_out_enabled;
    u32 param_buf[64]; u32 param_pos, param_need;
} psx_mdec;

/* ----------------------------------------------------------------- SIO --- */
typedef struct {
    u16 stat, mode, ctrl, baud;
    u8 rx; bool rx_full;
    int device;             /* 0 none, 1 pad, 2 card */
    int step;
    u8 tx_data;
    bool ack_pending;
    u16 buttons[2];         /* active-low digital button state per port */
    bool pad_connected[2];
} psx_sio;

/* ------------------------------------------------------------- HLE BIOS --- */
typedef struct psx_hle psx_hle;

/* Host interfaces --------------------------------------------------------- */
typedef struct {
    void *user;
    /* returns active-low 16-bit button mask for port 0/1 (0xFFFF = none) */
    u16 (*read_pad)(void *user, int port);
    bool (*pad_connected)(void *user, int port);
} psx_host;

/* Native override (reimplemented function) -------------------------------- */
typedef struct psx psx;
/* Native replacement for an original routine. Arguments are read from the
 * CPU registers/RAM; results written the same way the original does. Return
 * true when handled (the core then emulates "jr ra"); false to run the
 * original code instead. *cycles receives the modelled execution time. */
typedef bool (*psx_native_fn)(psx *p, u64 *cycles);

typedef struct {
    u32 addr;
    psx_native_fn fn;
    const char *name;
    u64 calls, declined;
    u64 verified_ok, verified_bad;
    u64 orig_cycles, native_cycles; /* totals measured in verify mode */
    bool observe_only;   /* runs before the original (e.g. captures a table) */
} psx_override;

/* --------------------------------------------------------------- Machine --- */
struct psx {
    psx_cpu cpu;
    psx_gte gte;
    u8 *ram;
    u8 scratch[PSX_SCRATCH_SIZE];
    u8 *bios;             /* BIOS ROM area: unused (HLE kernel), reads return 0 */

    u32 i_stat, i_mask;
    u32 mem_ctrl[9], ram_size, cache_ctrl;

    psx_dma dma;
    psx_timer timers[3];
    psx_gpu gpu;
    psx_cdrom cd;
    psx_spu spu;
    psx_mdec mdec;
    psx_sio sio;
    psx_hle *hle;

    u64 cycles;
    u64 ev_time[EV_COUNT];
    u64 ev_due;            /* due time of the event being dispatched */
    u64 next_event;
    bool stop;            /* break out of the run loop */
    bool halted;          /* fatal error */
    char halt_reason[256];

    nr_disc *disc;
    psx_host host;

    /* overrides */
    psx_override *ovr; int novr, ovr_cap;
    u8 *ovr_bitmap;       /* one bit per RAM word: override present */
    bool ovr_verify;      /* compare each native call against the original */
    bool ovr_busy;        /* re-entrancy guard */

    /* diagnostics */
    u64 instr_count;
    bool trace;
    u32 watch_pc;
    u32 *pcprof;            /* diagnostic PC histogram (one counter per RAM word), sampled every GPU line */
    /* Time charged by a native routine. It is consumed while the CPU sits on
     * the routine's return address, so interrupts are still serviced during
     * it exactly as they would be during the original (interruptible) code. */
    u64 stall;
    u32 stall_pc;
    u32 watch_write;    /* debug: log writes to this RAM word */
    u32 cycles_per_instr;  /* base CPI (timing model) */
    u32 cpu_overclock;     /* enhancement: instructions per base cycle (1 = console) */
    u32 oc_count;
};

/* psx.c */
psx *psx_create(void);
/* Machine snapshot (savestate) in memory. Host settings (CPU speed, GPU
 * timing model, overrides, recorder, memory card files) are not part of it
 * and keep their current values when a snapshot is loaded. Snapshots contain
 * the console RAM - i.e. game code and data - so they are for local analysis
 * only and must never be distributed. */
bool psx_state_save(const psx *p, u8 **out, size_t *len);
bool psx_state_load(psx *p, const u8 *in, size_t len);
void spu_state_ext(psx *p, void **ptr, size_t *size);
void hle_state_blob(psx *p, void **ptr, size_t *size);
void hle_restore_host_fields(psx *p, const void *old_blob);
void psx_destroy(psx *p);
void psx_reset(psx *p);
void psx_attach_disc(psx *p, nr_disc *d);
/* Runs until the GPU reaches the next vblank (one video frame) */
void psx_run_frame(psx *p);
void psx_halt(psx *p, const char *fmt, ...) NR_PRINTF(2, 3);

/* scheduler */
void psx_schedule(psx *p, int ev, u64 delta);
void psx_unschedule(psx *p, int ev);
/* periodic sources: delta counted from the due time of the event being
 * handled, so lateness (e.g. after a native routine) never accumulates */
void psx_schedule_periodic(psx *p, int ev, u64 delta);
void psx_update_next_event(psx *p);
void psx_dispatch(psx *p);   /* process all due scheduler events */

/* interrupts */
void psx_irq(psx *p, int line);
void psx_update_irq(psx *p);

/* bus.c */
u32 bus_read32(psx *p, u32 addr);
u16 bus_read16(psx *p, u32 addr);
u8  bus_read8(psx *p, u32 addr);
void bus_write32(psx *p, u32 addr, u32 v);
void bus_write16(psx *p, u32 addr, u16 v);
void bus_write8(psx *p, u32 addr, u8 v);
/* Direct RAM helpers for HLE code (KUSEG/KSEG0/KSEG1 addresses). */
u8 *psx_ram_ptr(psx *p, u32 addr);   /* NULL if not RAM */
u32 psx_ram_r32(psx *p, u32 addr);
void psx_ram_w32(psx *p, u32 addr, u32 v);
u16 psx_ram_r16(psx *p, u32 addr);
void psx_ram_w16(psx *p, u32 addr, u16 v);
u8 psx_ram_r8(psx *p, u32 addr);
void psx_ram_w8(psx *p, u32 addr, u8 v);
bool psx_ram_strcpy_out(psx *p, u32 addr, char *out, size_t n);

/* cpu.c */
void cpu_reset(psx *p);
void cpu_run(psx *p, u64 until);
void cpu_exception(psx *p, u32 code);
void cpu_set_pc(psx *p, u32 pc);   /* jump (clears delay state) */
void cpu_icache_flush(psx *p);     /* forget the instruction cache tags (timing model) */
const char *cpu_reg_name(int r);
void cpu_disasm(u32 pc, u32 instr, char *out, size_t n);

/* gte.c */
void gte_reset(psx_gte *g);
u32 gte_read_data(psx_gte *g, int r);
void gte_write_data(psx_gte *g, int r, u32 v);
u32 gte_read_ctrl(psx_gte *g, int r);
void gte_write_ctrl(psx_gte *g, int r, u32 v);
int gte_command(psx_gte *g, u32 cmd);   /* returns cycles */

/* dma.c */
void dma_reset(psx *p);
u32 dma_read(psx *p, u32 addr);
void dma_write(psx *p, u32 addr, u32 v);
void dma_event(psx *p);

/* timers.c */
void timers_reset(psx *p);
u32 timers_read(psx *p, u32 addr);
void timers_write(psx *p, u32 addr, u32 v);
void timers_event(psx *p, int t);
void timers_hblank(psx *p);
void timers_vblank(psx *p, bool start);

/* gpu.c */
void gpu_init(psx *p);
void gpu_free(psx *p);
void gpu_reset(psx *p);
u32 gpu_read(psx *p, u32 reg);
void gpu_write_gp0(psx *p, u32 v);
void gpu_write_gp1(psx *p, u32 v);
void gpu_event(psx *p);
bool gpu_busy(psx *p);
u64 gpu_busy_remaining(psx *p);
u32 gpu_dma_read(psx *p);
/* Returns display size and fills rgba (caller provides 1024*512*4 bytes). */
void gpu_get_display(psx *p, u32 *rgba, int *w, int *h);
void gpu_record_display(psx *p);   /* fills gpu.rec->display (called at vblank) */
void gpu_get_vram_rgba(psx *p, u32 *rgba);  /* full 1024x512 */

/* cdrom.c */
void cdrom_reset(psx *p);
u8 cdrom_read(psx *p, u32 reg);
void cdrom_write(psx *p, u32 reg, u8 v);
void cdrom_event(psx *p);
void cdrom_read_event(psx *p);
u32 cdrom_dma_read(psx *p);

/* spu.c */
void spu_init(psx *p);
void spu_free(psx *p);
void spu_reset(psx *p);
u16 spu_read(psx *p, u32 addr);
void spu_write(psx *p, u32 addr, u16 v);
void spu_event(psx *p);
void spu_dma_write(psx *p, u32 v);
u32 spu_dma_read(psx *p);
/* Pull up to n stereo frames (44.1 kHz) into out. Returns frames copied. */
u32 spu_pull_audio(psx *p, s16 *out, u32 n);
u32 spu_audio_available(psx *p);

/* mdec.c */
void mdec_init(psx *p);
void mdec_free(psx *p);
void mdec_reset(psx *p);
u32 mdec_read(psx *p, u32 reg);
void mdec_write(psx *p, u32 reg, u32 v);
void mdec_dma_write(psx *p, u32 v);
u32 mdec_dma_read(psx *p);
/* Standalone decoder used by the format tools (no machine needed): decodes
 * RLE halfwords of colour macroblocks (column-major order) into RGBA. */
int mdec_decode_rle_image(const u16 *rle, u32 nhalf, int width, int height, const u8 *qy, const u8 *quv, u32 *rgba);

/* sio.c */
void sio_reset(psx *p);
u32 sio_read(psx *p, u32 addr, int width);
void sio_write(psx *p, u32 addr, u32 v, int width);
void sio_event(psx *p);

/* hle_bios.c */
bool hle_init(psx *p);
void hle_free(psx *p);
void hle_reset(psx *p);
/* Executes HLE trap with given id (from the reserved opcode). */
void hle_trap(psx *p, u32 id);
/* Loads PS-X EXE from the disc via SYSTEM.CNF and prepares the CPU. */
bool hle_boot_disc(psx *p, char *err, size_t errlen);
void hle_vblank(psx *p);
void hle_dump_state(psx *p);
/* card image path for memory card slot 0/1 (NULL = none) */
void hle_set_memcard(psx *p, int slot, const char *path);
void hle_flush_memcards(psx *p);
void hle_call_capture(psx *p, u32 func, psx_cpu *after);

/* overrides.c */
void psx_override_add(psx *p, u32 addr, psx_native_fn fn, const char *name, bool observe_only);
bool psx_override_try(psx *p, u32 pc);
void psx_override_report(psx *p);

#endif
