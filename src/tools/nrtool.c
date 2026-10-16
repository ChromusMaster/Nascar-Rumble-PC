/* nrtool - diagnostics and format inspection for the user's own disc.
 * All commands read directly from the disc image; nothing is copied into the
 * project. Output files (PNG/WAV) are written only where the user asks. */
#include "../common/common.h"
#include "../disc/disc.h"
#include "../disc/gameid.h"
#include "../psx/psx.h"
#include "../formats/formats.h"
#include "../mods/mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
        "nrtool - inspection of the NASCAR Rumble disc (the user's own copy)\n"
        "Usage: nrtool <command> <disc.cue|.bin> [args]\n\n"
        "  info                       identify the disc and check file sizes\n"
        "  verify                     check the CRC32 of every file\n"
        "  ls                         list the ISO9660 file system\n"
        "  exe                        PS-X executable header\n"
        "  disasm ADDR N              disassemble N instructions of the executable\n"
        "  ctrl FILE                  list the blocks of a CTRL/SHOC container (.TRK, GLBLDATA.PSX)\n"
        "  lsc FILE OUT.png           decode a .LSC screen (native) to PNG\n"
        "  swvr FILE                  list the blocks of an .AV file (EA SWVR)\n"
        "  swvr-wav FILE OUT.wav      decode the audio of an .AV file to WAV\n"
        "  export-text OUT.txt        create the translation template (mods/Language) from the disc\n");
}

static nr_disc *open_disc(const char *path)
{
    char err[512];
    nr_disc *d = nr_disc_open(path, err, sizeof(err));
    if (!d) { fprintf(stderr, "ERROR: %s\n", err); return NULL; }
    if (!nr_iso_mount(d, err, sizeof(err))) { fprintf(stderr, "ERROR: %s\n", err); nr_disc_close(d); return NULL; }
    return d;
}

static int cmd_info(nr_disc *d, bool full)
{
    nr_game_report r;
    nr_game_identify(d, full, &r);
    printf("Image:       %s\n", nr_disc_image_path(d));
    printf("Sectors:     %u (%s)\n", nr_disc_sector_count(d), nr_disc_is_raw(d) ? "2352" : "2048");
    printf("Volume:      %s\n", nr_iso_volume_id(d));
    printf("Boot:        %s\n", r.boot_path);
    printf("SHA-1 EXE:   %s (%s)\n", r.exe_sha1, r.exe_sha1_match ? "matches" : "DIFFERENT");
    printf("Files:       %d expected, %d missing, %d size mismatch, %d CRC mismatch (%d checked)\n",
           r.files_expected, r.files_missing, r.files_size_mismatch, r.files_crc_mismatch, r.files_checked_crc);
    printf("Result:      %s\n", r.message);
    return r.status == NR_GAME_OK ? 0 : 1;
}

static int cmd_ls(nr_disc *d)
{
    int i;
    for (i = 0; i < nr_iso_count(d); i++) {
        const nr_iso_entry *e = nr_iso_get(d, i);
        printf("%-36s %s lba=%6u size=%10u\n", e->path, e->is_dir ? "<DIR>" : "     ", e->lba, e->size);
    }
    return 0;
}

static u8 *load_exe(nr_disc *d, u32 *size)
{
    u8 *exe;
    if (!nr_iso_read_file(d, NR_EXPECTED_SERIAL, &exe, size)) { fprintf(stderr, "ERROR: executable not found\n"); return NULL; }
    return exe;
}

static int cmd_exe(nr_disc *d)
{
    u32 sz;
    u8 *e = load_exe(d, &sz);
    if (!e) return 1;
    printf("Magic:   %.8s\n", (char *)e);
    printf("PC:      %08X\nGP:      %08X\nText:    %08X (%u bytes)\nBSS:     %08X (%u bytes)\nStack:   %08X+%X\n",
           nr_rd32(e + 0x10), nr_rd32(e + 0x14), nr_rd32(e + 0x18), nr_rd32(e + 0x1C), nr_rd32(e + 0x28), nr_rd32(e + 0x2C),
           nr_rd32(e + 0x30), nr_rd32(e + 0x34));
    printf("Region:  %.60s\n", (char *)e + 0x4C);
    free(e);
    return 0;
}

static int cmd_disasm(nr_disc *d, u32 addr, u32 n)
{
    u32 sz, i, base;
    u8 *e = load_exe(d, &sz);
    if (!e) return 1;
    base = nr_rd32(e + 0x18);
    for (i = 0; i < n; i++) {
        u32 a = addr + i * 4, off = a - base;
        char buf[96];
        u32 w;
        if (a < base || off + 4 > nr_rd32(e + 0x1C)) break;
        w = nr_rd32(e + 0x800 + off);
        cpu_disasm(a, w, buf, sizeof(buf));
        printf("%08X: %08X  %s\n", a, w, buf);
    }
    free(e);
    return 0;
}

static int cmd_ctrl(nr_disc *d, const char *path)
{
    u8 *buf;
    u32 sz;
    if (!nr_iso_read_file(d, path, &buf, &sz)) { fprintf(stderr, "ERROR: file not found: %s\n", path); return 1; }
    nr_ctrl_dump(buf, sz, stdout);
    free(buf);
    return 0;
}

static int cmd_lsc(nr_disc *d, const char *path, const char *out)
{
    u8 *buf;
    u32 sz;
    nr_image img;
    char err[256];
    if (!nr_iso_read_file(d, path, &buf, &sz)) { fprintf(stderr, "ERROR: file not found: %s\n", path); return 1; }
    if (!nr_lsc_decode(buf, sz, &img, err, sizeof(err))) { fprintf(stderr, "ERROR: %s\n", err); free(buf); return 1; }
    free(buf);
    if (!nr_write_png(out, img.rgba, img.w, img.h)) { fprintf(stderr, "ERROR: failed to write %s\n", out); nr_image_free(&img); return 1; }
    printf("%s: %dx%d -> %s\n", path, img.w, img.h, out);
    nr_image_free(&img);
    return 0;
}

static int cmd_swvr(nr_disc *d, const char *path, const char *wav)
{
    u8 *buf;
    u32 sz;
    int rc;
    if (!nr_iso_read_file(d, path, &buf, &sz)) { fprintf(stderr, "ERROR: file not found: %s\n", path); return 1; }
    rc = wav ? nr_swvr_to_wav(buf, sz, wav, stdout) : nr_swvr_dump(buf, sz, stdout);
    free(buf);
    return rc;
}

int main(int argc, char **argv)
{
    nr_disc *d;
    int rc = 2;
    nr_utf8_args(&argc, &argv);
    if (argc < 3) { usage(); return 2; }
    d = open_disc(argv[2]);
    if (!d) return 1;
    if (strcmp(argv[1], "info") == 0) rc = cmd_info(d, false);
    else if (strcmp(argv[1], "verify") == 0) rc = cmd_info(d, true);
    else if (strcmp(argv[1], "ls") == 0) rc = cmd_ls(d);
    else if (strcmp(argv[1], "exe") == 0) rc = cmd_exe(d);
    else if (strcmp(argv[1], "disasm") == 0 && argc >= 5) rc = cmd_disasm(d, (u32)strtoul(argv[3], NULL, 16), (u32)strtoul(argv[4], NULL, 0));
    else if (strcmp(argv[1], "ctrl") == 0 && argc >= 4) rc = cmd_ctrl(d, argv[3]);
    else if (strcmp(argv[1], "lsc") == 0 && argc >= 5) rc = cmd_lsc(d, argv[3], argv[4]);
    else if (strcmp(argv[1], "swvr") == 0 && argc >= 4) rc = cmd_swvr(d, argv[3], NULL);
    else if (strcmp(argv[1], "swvr-wav") == 0 && argc >= 5) rc = cmd_swvr(d, argv[3], argv[4]);
    else if (strcmp(argv[1], "export-text") == 0 && argc >= 4) {
        char err[256];
        if (nr_mods_export_text(d, argv[3], err, sizeof(err))) { printf("template written to %s\n", argv[3]); rc = 0; }
        else { fprintf(stderr, "ERROR: %s\n", err); rc = 1; }
    }
    else usage();
    nr_disc_close(d);
    return rc;
}
