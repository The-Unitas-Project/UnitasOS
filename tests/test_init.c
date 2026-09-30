#include <assert.h>
#include <kern/init.h>
#include <stdio.h>
#include <string.h>

struct probe_state {
    const char *available[5];
    size_t count;
    size_t calls;
};

static int probe_path(const char *path, void *context) {
    struct probe_state *state = context;
    ++state->calls;
    for (size_t index = 0; index < state->count; ++index)
        if (state->available[index] &&
            strcmp(path, state->available[index]) == 0) return 1;
    return 0;
}

int main(void) {
    struct probe_state state = {
        .available = { "/bin/sh", "/sbin/init", "/bin/systemd" },
        .count = 3
    };
    assert(strcmp(unitas_select_init(probe_path, &state), "/bin/systemd") == 0);
    assert(state.calls == 1);

    state.calls = 0;
    state.available[2] = 0;
    assert(strcmp(unitas_select_init(probe_path, &state), "/sbin/init") == 0);
    assert(state.calls == 2);

    state.calls = 0;
    state.count = 1;
    state.available[0] = "/bin/sh";
    assert(strcmp(unitas_select_init(probe_path, &state), "/bin/sh") == 0);
    assert(state.calls == 5);
    state.calls = 0;
    state.count = 0;
    assert(unitas_select_init(probe_path, &state) == 0);
    assert(state.calls == 5);
    assert(unitas_select_init(0, &state) == 0);

    puts("init selection tests passed");
    return 0;
}
