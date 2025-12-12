#include <stdnoreturn.h>
#include <lib/misc.h>

noreturn void chainload(char *config, char *cmdline) {
    (void)config;
    (void)cmdline;
    panic(false, "chainload protocol is not supported in this BIOS build");
}
