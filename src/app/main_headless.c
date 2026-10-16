/* Headless runner: executes the game without window or audio. Used for
 * automated tests, regression screenshots and diagnostics. */
#include "app.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    nr_app a;
    int rc = 0;
    nr_utf8_args(&argc, &argv);
    memset(&a, 0, sizeof(a));
    nr_options_default(&a.opt);
    a.opt.headless = true;
    if (!nr_options_parse(&a.opt, argc, argv)) return 2;
    if (!a.opt.frames) a.opt.frames = 600;
    if (!nr_app_start(&a)) { nr_app_shutdown(&a); return 1; }
    while ((int)a.frame < a.opt.frames && !a.p->halted) nr_app_frame(&a);
    if (a.p->halted) rc = 3;
    nr_app_shutdown(&a);
    return rc;
}
