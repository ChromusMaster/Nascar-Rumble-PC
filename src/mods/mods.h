/* User mods (mods/ folder). See docs/MODS.md.
 *
 *   mods/Language/<idioma>/text.txt   translated texts
 *   mods/Language/<idioma>/files/...  replacement files mirroring the disc
 *                                     layout (e.g. files/CW/HELPSCRN/HELP1.LSC)
 *   mods/<categoria>/<nome>/...       same structure, enabled with
 *                                     mods.load in nrengine.ini
 *
 * Nothing on the user's disc image is modified: replacement files are served
 * by the virtual CD layer (nr_disc_overlay_file) and texts are placed in
 * memory while the game runs. */
#ifndef NR_MODS_H
#define NR_MODS_H

#include "../psx/psx.h"
#include "../disc/disc.h"

typedef struct nr_mods nr_mods;

/* language: folder under mods/Language ("" = none); extra: comma separated
 * list of other mod folders relative to mods/ ("" = none). Applies file
 * overlays to the disc. Returns NULL when there is nothing to load. */
nr_mods *nr_mods_open(const char *language, const char *extra, nr_disc *disc);
/* after the executable is loaded: texts */
void nr_mods_attach(nr_mods *m, psx *p);
void nr_mods_frame(nr_mods *m, psx *p);
void nr_mods_close(nr_mods *m);

/* Template of the game's texts read from the user's disc (nrtool). */
bool nr_mods_export_text(nr_disc *d, const char *out_path, char *err, size_t errlen);

#endif
