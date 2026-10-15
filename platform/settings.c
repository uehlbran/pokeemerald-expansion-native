/*
 * settings.c: the player's settings file (settings.h has the format).
 *
 * Written from the format settings.h fixes; the key names are SDL's, looked
 * up through input.h, which owns the keyboard.
 */
#include "settings.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "host_file.h"
#include "options.h"
#include "overlay.h"

#define MODULE "settings"

#define SCALE_SETTING "scale"
#define COMMENT_CHAR '#'
#define SEPARATOR_CHAR '='
#define DECIMAL_BASE 10

/* U+FEFF in UTF-8, which some editors (Notepad before 2019) write at the
 * start of a file. */
static const char byte_order_mark[] = "\xEF\xBB\xBF";
#define BYTE_ORDER_MARK_SIZE (sizeof byte_order_mark - 1u)

/* Room for a key's name and its NUL: SDL's longest are some 30 characters. */
#define KEY_NAME_SIZE 64u

/* The bytes a problem's text keeps as they are, printable ASCII; any other
 * (a control character, an escape sequence, a byte of UTF-8) is reported as
 * PROBLEM_TEXT_REPLACEMENT, so that a bad line cannot write to the terminal. */
#define PROBLEM_TEXT_FIRST ' '
#define PROBLEM_TEXT_LAST '~'
#define PROBLEM_TEXT_REPLACEMENT '?'

/* A stretch of the file's text, not NUL-terminated. */
struct span {
    const char *text;
    size_t length;
};

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static struct span trim(const char *text, size_t length)
{
    while (length > 0 && is_blank(text[0])) {
        text++;
        length--;
    }
    while (length > 0 && is_blank(text[length - 1u]))
        length--;
    return (struct span){.text = text, .length = length};
}

/* True if span is word (lower case) in any case. The program never calls
 * setlocale, so tolower is the C locale's: ASCII letters only. */
static bool span_is(struct span span, const char *word)
{
    size_t i = 0;
    for (; i < span.length; i++) {
        if (word[i] == '\0' || tolower((unsigned char)span.text[i]) != (unsigned char)word[i])
            return false;
    }
    return word[i] == '\0';
}

/* span as a string in out; false if it does not fit or holds a NUL. */
static bool span_to_string(struct span span, char *out, size_t size)
{
    if (span.length >= size || memchr(span.text, '\0', span.length) != NULL)
        return false;
    memcpy(out, span.text, span.length);
    out[span.length] = '\0';
    return true;
}

static void add_problem(struct settings *settings, uint32_t line, enum settings_problem_kind kind, struct span text)
{
    if (settings->problem_count < SETTINGS_MAX_PROBLEMS) {
        struct settings_problem *const problem = &settings->problems[settings->problem_count];
        const size_t length = text.length < sizeof problem->text - 1u ? text.length : sizeof problem->text - 1u;
        problem->line = line;
        problem->kind = kind;
        for (size_t i = 0; i < length; i++) {
            const char byte = text.text[i];
            const bool printable = byte >= PROBLEM_TEXT_FIRST && byte <= PROBLEM_TEXT_LAST;
            problem->text[i] = printable ? byte : PROBLEM_TEXT_REPLACEMENT;
        }
        problem->text[length] = '\0';
    }
    settings->problem_count++;
}

/* A whole number from 1 to OPTIONS_MAX_SCALE, digits only. */
static bool parse_scale(struct span value, int *out)
{
    int scale = 0;
    for (size_t i = 0; i < value.length; i++) {
        const char digit = value.text[i];
        if (digit < '0' || digit > '9')
            return false;
        scale = scale * DECIMAL_BASE + (digit - '0');
        if (scale > OPTIONS_MAX_SCALE)
            return false;
    }
    if (scale < 1)
        return false;
    *out = scale;
    return true;
}

/* The index in input_buttons() of the button called name, or
 * INPUT_BUTTON_COUNT. */
static size_t find_button(struct span name)
{
    const struct input_button *const buttons = input_buttons();
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        if (span_is(name, buttons[i].setting))
            return i;
    }
    return INPUT_BUTTON_COUNT;
}

static void apply_key(struct settings *settings, uint32_t line, size_t button, struct span value)
{
    char name[KEY_NAME_SIZE];
    const int scancode = span_to_string(value, name, sizeof name) ? input_scancode_from_name(name) : INPUT_NO_SCANCODE;
    if (scancode == INPUT_NO_SCANCODE)
        add_problem(settings, line, SETTINGS_UNKNOWN_KEY, value);
    else if (scancode == OVERLAY_TOGGLE_SCANCODE)
        add_problem(settings, line, SETTINGS_RESERVED_KEY, value);
    else
        settings->keymap.scancodes[button] = scancode;
}

static void parse_line(struct settings *settings, uint32_t line, struct span raw)
{
    const struct span text = trim(raw.text, raw.length);
    if (text.length == 0 || text.text[0] == COMMENT_CHAR)
        return;
    const char *const separator = memchr(text.text, SEPARATOR_CHAR, text.length);
    if (separator == NULL) {
        add_problem(settings, line, SETTINGS_NOT_A_SETTING_LINE, text);
        return;
    }
    const size_t name_length = (size_t)(separator - text.text);
    const struct span name = trim(text.text, name_length);
    const struct span value = trim(separator + 1, text.length - name_length - 1u);
    if (name.length == 0 || value.length == 0) {
        add_problem(settings, line, SETTINGS_NOT_A_SETTING_LINE, text);
        return;
    }
    if (span_is(name, SCALE_SETTING)) {
        if (!parse_scale(value, &settings->scale))
            add_problem(settings, line, SETTINGS_BAD_SCALE, value);
        return;
    }
    const size_t button = find_button(name);
    if (button == INPUT_BUTTON_COUNT)
        add_problem(settings, line, SETTINGS_UNKNOWN_SETTING, name);
    else
        apply_key(settings, line, button, value);
}

void settings_defaults(struct settings *out)
{
    memset(out, 0, sizeof *out);
    out->keymap = input_default_keymap();
    out->scale = OPTIONS_DEFAULT_SCALE;
}

void settings_parse(const char *text, size_t size, struct settings *out)
{
    settings_defaults(out);
    size_t position = 0;
    if (size >= BYTE_ORDER_MARK_SIZE && memcmp(text, byte_order_mark, BYTE_ORDER_MARK_SIZE) == 0)
        position = BYTE_ORDER_MARK_SIZE;
    uint32_t line = 0;
    while (position < size) {
        const char *const start = text + position;
        const char *const end = memchr(start, '\n', size - position);
        const size_t length = end != NULL ? (size_t)(end - start) : size - position;
        line++;
        parse_line(out, line, (struct span){.text = start, .length = length});
        position += length + (end != NULL ? 1u : 0u);
    }
}

static void report_call(const char *call, const char *path)
{
    (void)fprintf(stderr, MODULE ": %s(%s) failed: %s\n", call, path, strerror(errno));
}

/* Reads the file into text, at most size bytes; the count read, or -1
 * (reported). */
static ptrdiff_t read_file(int descriptor, const char *path, char *text, size_t size)
{
    size_t done = 0;
    while (done < size) {
        const ptrdiff_t got = host_file_read(descriptor, text + done, size - done);
        if (got < 0) {
            report_call("read", path);
            return -1;
        }
        if (got == 0)
            break;
        done += (size_t)got;
    }
    return (ptrdiff_t)done;
}

int settings_load(const char *path, struct settings *out)
{
    /* One byte more than the largest file, to tell that file from a larger one. */
    char text[SETTINGS_FILE_MAX_SIZE + 1u];
    settings_defaults(out);
    const int descriptor = host_file_open(path, O_RDONLY);
    if (descriptor < 0) {
        if (errno == ENOENT)
            return 0;
        report_call("open", path);
        return -1;
    }
    const ptrdiff_t size = read_file(descriptor, path, text, sizeof text);
    if (host_file_close(descriptor) != 0) {
        report_call("close", path);
        return -1;
    }
    if (size < 0)
        return -1;
    if ((size_t)size > SETTINGS_FILE_MAX_SIZE) {
        (void)fprintf(stderr, MODULE ": %s is larger than %u bytes, which no settings file is; not read\n", path,
                      SETTINGS_FILE_MAX_SIZE);
        return -1;
    }
    settings_parse(text, (size_t)size, out);
    out->found = true;
    settings_report(out, path, stderr);
    return 0;
}

static void print_setting_names(FILE *out)
{
    const struct input_button *const buttons = input_buttons();
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++)
        (void)fprintf(out, "%s, ", buttons[i].setting);
    (void)fputs(SCALE_SETTING, out);
}

static void print_problem(const struct settings_problem *problem, const char *path, FILE *out)
{
    (void)fprintf(out, MODULE ": %s:%lu: ", path, (unsigned long)problem->line);
    switch (problem->kind) {
    case SETTINGS_NOT_A_SETTING_LINE:
        (void)fprintf(out, "'%s' is not a 'setting = value' line", problem->text);
        break;
    case SETTINGS_UNKNOWN_SETTING:
        (void)fprintf(out, "'%s' is not a setting (", problem->text);
        print_setting_names(out);
        (void)fprintf(out, ")");
        break;
    case SETTINGS_UNKNOWN_KEY:
        (void)fprintf(out, "'%s' is not a key name SDL knows (such as Z, Return, Left Shift, Keypad 8)", problem->text);
        break;
    case SETTINGS_RESERVED_KEY:
        (void)fprintf(out, "'%s' opens the help overlay and cannot be a button's key", problem->text);
        break;
    case SETTINGS_BAD_SCALE:
    default:
        (void)fprintf(out, "the scale '%s' is not a whole number from 1 to %d", problem->text, OPTIONS_MAX_SCALE);
        break;
    }
    (void)fprintf(out, "; the line is ignored\n");
}

void settings_report(const struct settings *settings, const char *path, FILE *out)
{
    const size_t kept = settings->problem_count < SETTINGS_MAX_PROBLEMS ? settings->problem_count
                                                                        : SETTINGS_MAX_PROBLEMS;
    for (size_t i = 0; i < kept; i++)
        print_problem(&settings->problems[i], path, out);
    if (settings->problem_count > kept)
        (void)fprintf(out, MODULE ": %s: %llu more lines with problems, also ignored\n", path,
                      (unsigned long long)(settings->problem_count - kept));
}
