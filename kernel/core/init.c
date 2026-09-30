#include <kern/init.h>

const char *unitas_select_init(init_path_probe_t probe, void *context) {
    /* Prefer systemd. Keep the shell as a fallback for existing images. */
    static const char *const candidates[] = {
        "/bin/systemd",
        "/sbin/init",
        "/lib/systemd/systemd",
        "/bin/init",
        "/bin/sh"
    };

    if (!probe) return 0;
    for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]);
         ++index) {
        if (probe(candidates[index], context) > 0) return candidates[index];
    }
    return 0;
}
