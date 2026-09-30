#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define SH_LINE_CAPACITY 256
#define SH_ARGUMENT_CAPACITY 32

static char line[SH_LINE_CAPACITY];
static size_t line_length;
static bool previous_character_was_cr;

static int write_all(const char *text, size_t length) {
    size_t written = 0;
    while (written < length) {
        ssize_t amount = write(1, text + written, length - written);
        if (amount <= 0) return -1;
        written += (size_t)amount;
    }
    return 0;
}

static int print_text(const char *text) {
    return write_all(text, strlen(text));
}

static bool is_space(char character) {
    return character == ' ' || character == '\t';
}

/* Split one line into arguments. Support quotes and backslash escapes. */
static int split_arguments(char *text, char **arguments, size_t capacity) {
    size_t read_index = 0;
    size_t write_index = 0;
    size_t count = 0;

    while (text[read_index]) {
        while (is_space(text[read_index])) ++read_index;
        if (!text[read_index]) break;
        if (count == capacity) return -1;

        arguments[count++] = text + write_index;
        char quote = 0;
        while (text[read_index]) {
            char character = text[read_index++];
            if (!quote && is_space(character)) break;
            if (character == '\\' && quote != '\'') {
                if (!text[read_index]) return -1;
                text[write_index++] = text[read_index++];
            } else if (quote && character == quote) {
                quote = 0;
            } else if (!quote && (character == '\'' || character == '"')) {
                quote = character;
            } else {
                text[write_index++] = character;
            }
        }
        if (quote) return -1;
        text[write_index++] = 0;
    }

    return (int)count;
}

static bool make_program_path(const char *name, char path[SH_LINE_CAPACITY]) {
    size_t length = strlen(name);
    bool has_slash = false;
    for (size_t index = 0; index < length; ++index)
        if (name[index] == '/') has_slash = true;

    size_t prefix_length = has_slash ? 0 : sizeof("/bin/") - 1;
    if (prefix_length + length >= SH_LINE_CAPACITY) return false;
    if (prefix_length) memcpy(path, "/bin/", prefix_length);
    memcpy(path + prefix_length, name, length + 1);
    return true;
}

static void run_line(char *text) {
    char *tokens[SH_ARGUMENT_CAPACITY];
    int token_count = split_arguments(text, tokens,
                                      sizeof(tokens) / sizeof(tokens[0]));
    if (token_count < 0) {
        (void)print_text("sh: invalid command line\n");
        return;
    }
    if (!token_count) return;
    if (strcmp(tokens[0], "help") == 0) {
        (void)print_text("Programs in /bin: hello.elf, reboot, poweroff, shutdown, sh\n");
        (void)print_text("Use help for this list. Quote arguments or escape spaces with a backslash.\n");
        return;
    }

    char path[SH_LINE_CAPACITY];
    if (!make_program_path(tokens[0], path)) {
        (void)print_text("sh: command path is too long\n");
        return;
    }

    char *arguments[SH_ARGUMENT_CAPACITY + 1];
    arguments[0] = path;
    for (int index = 1; index < token_count; ++index)
        arguments[index] = tokens[index];
    arguments[token_count] = 0;

    static char path_environment[] = "PATH=/bin";
    static char home_environment[] = "HOME=/root";
    static char term_environment[] = "TERM=dumb";
    char *environment[] = {
        path_environment, home_environment, term_environment, 0
    };
    if (execve(path, arguments, environment) < 0) {
        (void)print_text("sh: cannot run ");
        (void)print_text(tokens[0]);
        (void)print_text("\n");
    }
}

/* Keep terminal input and echo in this user program. */
static void process_character(char character) {
    if (character == '\b' || character == '\x7f') {
        if (line_length) {
            --line_length;
            (void)write_all("\b \b", 3);
        }
        previous_character_was_cr = false;
        return;
    }
    if (character == '\n' && previous_character_was_cr) {
        previous_character_was_cr = false;
        return;
    }
    previous_character_was_cr = character == '\r';
    if (character == '\n' || character == '\r') {
        (void)write_all("\n", 1);
        line[line_length] = 0;
        run_line(line);
        line_length = 0;
        (void)print_text("unitas:/# ");
        return;
    }
    if (character == '\t') character = ' ';
    if ((unsigned char)character < 0x20 ||
        line_length + 1 >= sizeof(line)) return;
    line[line_length++] = character;
    (void)write_all(&character, 1);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    (void)print_text("unitas:/# ");
    for (;;) {
        char character;
        if (read(0, &character, 1) == 1) process_character(character);
    }
}
