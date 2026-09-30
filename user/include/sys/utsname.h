#ifndef UNITAS_USER_SYS_UTSNAME_H
#define UNITAS_USER_SYS_UTSNAME_H

#include <unitas/utsname.h>

#define UTSNAME_LENGTH UNITAS_UTSNAME_LENGTH
struct utsname {
    char sysname[UTSNAME_LENGTH];
    char nodename[UTSNAME_LENGTH];
    char release[UTSNAME_LENGTH];
    char version[UTSNAME_LENGTH];
    char machine[UTSNAME_LENGTH];
    char domainname[UTSNAME_LENGTH];
};

int uname(struct utsname *name);

#endif
