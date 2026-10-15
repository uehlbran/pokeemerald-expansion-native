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
#define FAST_FORWARD_SETTING "fast_forward"
#define FAST_FORWARD_TOGGLE_SETTING "fast_forward_toggle"
#define FAST_FORWARD_SPEED_SETTING "fast_forward_speed"
#define FAST_FORWARD_AUDIO_SETTING "fast_forward_audio"
#define FULLSCREEN_SETTING "fullscreen"
#define FULLSCREEN_KEY_SETTING "fullscreen_toggle"
#define AUTO_VALUE "auto"
#define YES_VALUE "yes"
#define NO_VALUE "no"
#define UNLIMITED_VALUE "unlimited"
#define PLAY_VALUE "play"
#define MUTE_VALUE "mute"
#define COMMENT_CHAR '#'
#define SEPARATOR_CHAR '='
#define DECIMAL_BASE 10

/* U+FEFF in UTF-8, which some editors (Notepad before 2019) write at the
 * start of a file. */
static const char byte_order_mark[] = "\xEF\xBB\xBF";
#define BYTE_ORDER_MARK_SIZE (sizeof byte_order_mark - 1u)

/* Room for a key's name and its NUL: SDL's longest are some 30 characters,
 * and a fast-forward key's modifiers add at most "ctrl+alt+shift+". */
#define KEY_NAME_SIZE FASTFORWARD_KEY_NAME_SIZE

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

/* The line that set a fast-forward key (0: none, the default), and its value. */
struct key_line {
    uint32_t line;
    struct span value;
};

/* What settings_parse knows as it goes: the settings, and where the two
 * fast-forward keys came from, for the check that they differ. */
struct parse {
    struct settings *settings;
    struct key_line hold;
    struct key_line toggle;
    struct key_line pad[CONTROLLER_FUNCTION_COUNT];
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
            if (byte >= PROBLEM_TEXT_FIRST && byte <= PROBLEM_TEXT_LAST)
                problem->text[i] = byte;
            else
                problem->text[i] = PROBLEM_TEXT_REPLACEMENT;
        }
        problem->text[length] = '\0';
    }
    settings->problem_count++;
}

/* A whole number from min to max, digits only. */
static bool parse_whole_number(struct span value, int min, int max, int *out)
{
    int number = 0;
    for (size_t i = 0; i < value.length; i++) {
        const char digit = value.text[i];
        if (digit < '0' || digit > '9')
            return false;
        number = number * DECIMAL_BASE + (digit - '0');
        if (number > max)
            return false;
    }
    if (number < min)
        return false;
    *out = number;
    return true;
}

/* 2 to FASTFORWARD_MAX_SPEED, or "unlimited". */
static bool parse_speed(struct span value, uint32_t *out)
{
    int speed = 0;
    if (span_is(value, UNLIMITED_VALUE))
        *out = FASTFORWARD_UNLIMITED;
    else if (parse_whole_number(value, FASTFORWARD_MIN_SPEED, FASTFORWARD_MAX_SPEED, &speed))
        *out = (uint32_t)speed;
    else
        return false;
    return true;
}

static bool parse_audio(struct span value, enum fastforward_audio *out)
{
    if (span_is(value, PLAY_VALUE))
        *out = FASTFORWARD_AUDIO_PLAY;
    else if (span_is(value, MUTE_VALUE))
        *out = FASTFORWARD_AUDIO_MUTE;
    else
        return false;
    return true;
}

static bool parse_fullscreen(struct span value, enum fullscreen_setting *out)
{
    if (span_is(value, AUTO_VALUE))
        *out = FULLSCREEN_AUTO;
    else if (span_is(value, YES_VALUE))
        *out = FULLSCREEN_YES;
    else if (span_is(value, NO_VALUE))
        *out = FULLSCREEN_NO;
    else
        return false;
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

static void apply_fullscreen_key(struct settings *settings, uint32_t line, struct span value)
{
    char name[KEY_NAME_SIZE];
    const int scancode = span_to_string(value, name, sizeof name) ? input_scancode_from_name(name) : INPUT_NO_SCANCODE;
    if (scancode == INPUT_NO_SCANCODE)
        add_problem(settings, line, SETTINGS_UNKNOWN_KEY, value);
    else if (scancode == OVERLAY_TOGGLE_SCANCODE)
        add_problem(settings, line, SETTINGS_RESERVED_KEY, value);
    else
        settings->fullscreen_key = scancode;
}

static void apply_pad_control(struct parse *parse, uint32_t line, size_t function, struct span value)
{
    char name[KEY_NAME_SIZE];
    const int control = span_to_string(value, name, sizeof name) ? controller_control_from_name(name)
                                                                 : CONTROLLER_NO_CONTROL;
    if (control == CONTROLLER_NO_CONTROL) {
        add_problem(parse->settings, line, SETTINGS_UNKNOWN_CONTROL, value);
    } else {
        parse->settings->pad.controls[function] = control;
        parse->pad[function] = (struct key_line){.line = line, .value = value};
    }
}

/* The function whose setting is called name, or CONTROLLER_FUNCTION_COUNT. */
static size_t find_pad_function(struct span name)
{
    const struct controller_function_info *const functions = controller_functions();
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++) {
        if (span_is(name, functions[i].setting))
            return i;
    }
    return CONTROLLER_FUNCTION_COUNT;
}

/* Of two controller functions on one button, the one the later line set, or
 * CONTROLLER_FUNCTION_COUNT if there are not two. */
static size_t find_pad_clash(const struct parse *parse)
{
    const struct controller_bindings *const pad = &parse->settings->pad;
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++) {
        for (size_t j = 0; j < i; j++) {
            if (pad->controls[i] == pad->controls[j])
                return parse->pad[i].line > parse->pad[j].line ? i : j;
        }
    }
    return CONTROLLER_FUNCTION_COUNT;
}

/* The later line's function goes back to its default (settings.h). Each
 * pass takes one function off a custom button, so the passes end. */
static void keep_pad_apart(struct parse *parse)
{
    const struct controller_bindings defaults = controller_default_bindings();
    for (size_t later = find_pad_clash(parse); later != CONTROLLER_FUNCTION_COUNT; later = find_pad_clash(parse)) {
        add_problem(parse->settings, parse->pad[later].line, SETTINGS_SAME_CONTROL, parse->pad[later].value);
        parse->settings->pad.controls[later] = defaults.controls[later];
        parse->pad[later].line = 0;
    }
}

static void apply_fast_forward_key(struct parse *parse, uint32_t line, struct span value, struct fastforward_key *key,
                                   struct key_line *from)
{
    char name[KEY_NAME_SIZE];
    struct fastforward_key read;
    if (!span_to_string(value, name, sizeof name) || !fastforward_key_from_name(name, &read)) {
        add_problem(parse->settings, line, SETTINGS_UNKNOWN_KEY, value);
    } else if (read.scancode == OVERLAY_TOGGLE_SCANCODE) {
        add_problem(parse->settings, line, SETTINGS_RESERVED_KEY, value);
    } else {
        *key = read;
        *from = (struct key_line){.line = line, .value = value};
    }
}

/* If the two keys are one, the later line's goes back to its default
 * (settings.h). */
static bool keep_apart(struct parse *parse)
{
    struct fastforward_settings *const keys = &parse->settings->fast_forward;
    if (!fastforward_keys_equal(&keys->hold, &keys->toggle))
        return true;
    const struct fastforward_settings defaults = fastforward_defaults();
    if (parse->hold.line > parse->toggle.line) {
        add_problem(parse->settings, parse->hold.line, SETTINGS_SAME_FAST_FORWARD_KEY, parse->hold.value);
        keys->hold = defaults.hold;
        parse->hold.line = 0;
    } else {
        add_problem(parse->settings, parse->toggle.line, SETTINGS_SAME_FAST_FORWARD_KEY, parse->toggle.value);
        keys->toggle = defaults.toggle;
        parse->toggle.line = 0;
    }
    return false;
}

/* The settings other than the buttons; false if name is none of them. */
static bool apply_named(struct parse *parse, uint32_t line, struct span name, struct span value)
{
    struct settings *const settings = parse->settings;
    if (span_is(name, SCALE_SETTING)) {
        if (!parse_whole_number(value, 1, OPTIONS_MAX_SCALE, &settings->scale))
            add_problem(settings, line, SETTINGS_BAD_SCALE, value);
    } else if (span_is(name, FAST_FORWARD_SETTING)) {
        apply_fast_forward_key(parse, line, value, &settings->fast_forward.hold, &parse->hold);
    } else if (span_is(name, FAST_FORWARD_TOGGLE_SETTING)) {
        apply_fast_forward_key(parse, line, value, &settings->fast_forward.toggle, &parse->toggle);
    } else if (span_is(name, FAST_FORWARD_SPEED_SETTING)) {
        if (!parse_speed(value, &settings->fast_forward.speed))
            add_problem(settings, line, SETTINGS_BAD_SPEED, value);
    } else if (span_is(name, FAST_FORWARD_AUDIO_SETTING)) {
        if (!parse_audio(value, &settings->fast_forward.audio))
            add_problem(settings, line, SETTINGS_BAD_AUDIO, value);
    } else if (span_is(name, FULLSCREEN_SETTING)) {
        if (!parse_fullscreen(value, &settings->fullscreen))
            add_problem(settings, line, SETTINGS_BAD_FULLSCREEN, value);
    } else if (span_is(name, FULLSCREEN_KEY_SETTING)) {
        apply_fullscreen_key(settings, line, value);
    } else {
        return false;
    }
    return true;
}

static void parse_line(struct parse *parse, uint32_t line, struct span raw)
{
    struct settings *const settings = parse->settings;
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
    if (apply_named(parse, line, name, value))
        return;
    const size_t pad_function = find_pad_function(name);
    if (pad_function != CONTROLLER_FUNCTION_COUNT) {
        apply_pad_control(parse, line, pad_function, value);
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
    out->fast_forward = fastforward_defaults();
    out->pad = controller_default_bindings();
    out->fullscreen = FULLSCREEN_AUTO;
    out->fullscreen_key = FULLSCREEN_DEFAULT_SCANCODE;
}

void settings_parse(const char *text, size_t size, struct settings *out)
{
    struct parse parse = {.settings = out};
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
        parse_line(&parse, line, (struct span){.text = start, .length = length});
        position += length + (end != NULL ? 1u : 0u);
    }
    /* Twice: the default a refused line leaves may be the other line's key. */
    if (!keep_apart(&parse))
        (void)keep_apart(&parse);
    keep_pad_apart(&parse);
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
    (void)fputs(SCALE_SETTING ", " FAST_FORWARD_SETTING ", " FAST_FORWARD_TOGGLE_SETTING ", " FAST_FORWARD_SPEED_SETTING
                              ", " FAST_FORWARD_AUDIO_SETTING ", " FULLSCREEN_SETTING ", " FULLSCREEN_KEY_SETTING,
                out);
    const struct controller_function_info *const functions = controller_functions();
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++)
        (void)fprintf(out, ", %s", functions[i].setting);
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
        (void)fprintf(out,
                      "'%s' is not a key name SDL knows (such as Z, Return, Left Shift, Keypad 8, and for "
                      "fast-forward Shift+Tab)",
                      problem->text);
        break;
    case SETTINGS_RESERVED_KEY:
        (void)fprintf(out, "'%s' opens the help overlay and cannot be a button's or fast-forward's key", problem->text);
        break;
    case SETTINGS_BAD_SPEED:
        (void)fprintf(out, "the fast-forward speed '%s' is not 2, 3, 4 or " UNLIMITED_VALUE, problem->text);
        break;
    case SETTINGS_BAD_AUDIO:
        (void)fprintf(out, "the fast-forward audio '%s' is not " PLAY_VALUE " or " MUTE_VALUE, problem->text);
        break;
    case SETTINGS_SAME_FAST_FORWARD_KEY:
        (void)fprintf(out,
                      "'%s' is the other fast-forward key too: one key cannot both hold and toggle, and this one "
                      "keeps its default",
                      problem->text);
        break;
    case SETTINGS_UNKNOWN_CONTROL:
        (void)fprintf(out,
                      "'%s' is not a controller button name SDL knows (such as x, y, leftstick, rightstick, "
                      "dpup, lefttrigger, righttrigger)",
                      problem->text);
        break;
    case SETTINGS_SAME_CONTROL:
        (void)fprintf(out, "'%s' is another function's controller button too: one button serves one function, and this one keeps its default",
                      problem->text);
        break;
    case SETTINGS_BAD_FULLSCREEN:
        (void)fprintf(out, "the fullscreen setting '%s' is not " YES_VALUE ", " NO_VALUE " or " AUTO_VALUE,
                      problem->text);
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
