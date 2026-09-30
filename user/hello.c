#include <stdio.h>

int main(int argc, char **argv) {
    if (puts("Hello from a Unitas ring 3 program") < 0) return 1;
    for (int index = 1; index < argc; ++index)
        if (puts(argv[index]) < 0) return 1;
    return 0;
}
