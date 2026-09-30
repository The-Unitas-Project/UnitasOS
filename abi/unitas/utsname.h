#ifndef UNITAS_ABI_UTSNAME_H
#define UNITAS_ABI_UTSNAME_H

#define UNITAS_UTSNAME_LENGTH 65

struct unitas_utsname {
    char system[UNITAS_UTSNAME_LENGTH];
    char node[UNITAS_UTSNAME_LENGTH];
    char release[UNITAS_UTSNAME_LENGTH];
    char version[UNITAS_UTSNAME_LENGTH];
    char machine[UNITAS_UTSNAME_LENGTH];
    char domain[UNITAS_UTSNAME_LENGTH];
};

#endif
