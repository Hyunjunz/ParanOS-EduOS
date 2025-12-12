#ifndef PROTOS__LIMINE_H__
#define PROTOS__LIMINE_H__

/* Wrapper to use the actual Limine protocol definitions shipped in the repo. */
#include "../limine/limine.h"

#include <stdnoreturn.h>

noreturn void limine_load(char *config, char *cmdline);

#endif
