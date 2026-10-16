/* Starting another program (launcher <-> engine) and the launcher session
 * check.
 *
 * The engine window (nrengine) only runs when started by the launcher
 * (nrlauncher): the launcher creates a random session token, passes it in
 * the environment variable NRENGINE_SESSION and as --launcher-session; the
 * engine compares both. This is a usage convention (it keeps settings in one
 * place), not a security mechanism. */
#ifndef NR_PROC_H
#define NR_PROC_H

#include "common.h"

#define NR_SESSION_ENV "NRENGINE_SESSION"

typedef struct nr_proc nr_proc;

/* argv[0] is the program path; args are UTF-8. env_name/env_value may be NULL. */
nr_proc *nr_proc_spawn(const char *const *argv, const char *env_name, const char *env_value, char *err, size_t errlen);
/* true once the process ended; *code receives its exit code */
bool nr_proc_done(nr_proc *p, int *code);
void nr_proc_free(nr_proc *p);

/* 32 random hex characters */
void nr_session_token(char out[33]);
bool nr_session_valid(const char *arg_token);

/* path of a sibling executable ("nrlauncher" -> ".../nrlauncher[.exe]") */
void nr_sibling_exe(char *out, size_t n, const char *name);

#endif
