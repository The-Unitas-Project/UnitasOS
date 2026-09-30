#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>

/* Use argv[0] so the same image can provide reboot, poweroff, and shutdown. */
int main(int argc, char **argv) {
    (void)argc;
    const char *name = argv[0];
    const char *base = name;
    for (const char *cursor = name; *cursor; ++cursor)
        if (*cursor == '/') base = cursor + 1;

    int command;
    if (strcmp(base, "reboot") == 0) command = LINUX_REBOOT_CMD_RESTART;
    else if (strcmp(base, "poweroff") == 0 || strcmp(base, "shutdown") == 0)
        command = LINUX_REBOOT_CMD_POWER_OFF;
    else {
        puts("run this program as reboot, poweroff, or shutdown");
        return 2;
    }

    if (reboot(command) < 0) {
        puts("power command failed or is unsupported");
        return 1;
    }
    return 0;
}
