#ifndef UNITAS_USER_SYS_REBOOT_H
#define UNITAS_USER_SYS_REBOOT_H

#define LINUX_REBOOT_CMD_RESTART 0x01234567
#define LINUX_REBOOT_CMD_HALT 0xcdef0123
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc

/* Send one Linux reboot command with the standard magic values. */
int reboot(int command);

#endif
