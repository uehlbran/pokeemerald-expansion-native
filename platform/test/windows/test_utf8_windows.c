/*
 * test_utf8_windows.c: the UTF-8 code page that the application manifest
 * gives every Windows executable (build_glue/windows_manifest.xml, linked
 * through platform/target.mk), tested where it matters:
 *  - the process's code page is UTF-8, which only a Windows that honours
 *    the manifest gives (version 1903 on);
 *  - main's argv: this executable started again with a non-ASCII argument,
 *    passed in UTF-16 to CreateProcessW, must receive exactly its UTF-8
 *    bytes. msvcrt builds argv from GetCommandLineA, which the code page
 *    makes UTF-8; that msvcrt keeps the bytes as they are is the claim no
 *    build here can check (docs/DECISIONS.md, "Narrow strings on Windows are
 *    UTF-8");
 *  - a save stored through a path with non-ASCII characters, in a directory
 *    the host layer made, loads back byte for byte, under the name it was
 *    given, as the wide API reads it back.
 * The texts are written with universal character names, so this file is
 * ASCII: u8"..." is the compiler's UTF-8 of them, L"..." its UTF-16, neither
 * the platform's conversion under test.
 */
// deps: save_file.c gba_memmap.c flash.c
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>

#include "host_file.h"
#include "save_file_fixture.h"
#include "windows_support.h"

/* "Unicode-" with four accented letters and two Kanji ("Nihon"), a file
 * name in Katakana ("seebu.sav"), and an argument of two Kanji, a space and
 * a character outside the Basic Multilingual Plane (a surrogate pair in
 * UTF-16). */
#define DIRECTORY_UTF8 u8"\u00DCn\u00EFc\u00F6d\u00E9-\u65E5\u672C"
#define DIRECTORY_UTF16 L"\u00DCn\u00EFc\u00F6d\u00E9-\u65E5\u672C"
#define FILE_UTF8 u8"\u30BB\u30FC\u30D6.sav"
#define FILE_UTF16 L"\u30BB\u30FC\u30D6.sav"
#define WIDE_ARGUMENT_UTF8 u8"\u65E5\u672C \U0001F600"
#define WIDE_ARGUMENT_UTF16 L"\u65E5\u672C \U0001F600"

/* The child that checks its argv: what it is started with after its name,
 * and the codes it ends with. */
#define ARGV_CHILD_MODE "--utf8-argv-child"
#define ARGV_CHILD_MODE_UTF16 L"--utf8-argv-child"
#define ARGV_OK 42
#define ARGV_WRONG_COUNT 30
#define ARGV_WRONG_BYTES 31

#define COMMAND_LINE_SIZE 1024u

PTEST(the_process_code_page_is_utf8)
{
    PT_ASSERT_EQ_U32(GetACP(), CP_UTF8);
}

/* Appends text to out (capacity characters, NUL-terminated); false if it
 * does not fit. */
static bool append(wchar_t *out, size_t capacity, const wchar_t *text)
{
    const size_t used = wcslen(out);
    const size_t length = wcslen(text);
    if (length >= capacity - used)
        return false;
    memcpy(out + used, text, (length + 1) * sizeof *text);
    return true;
}

/* main, for the child: argv[2] and argv[3] must be the UTF-8 of what the
 * parent passed. */
static int check_arguments(int argc, char **argv)
{
    if (argc != 4)
        return ARGV_WRONG_COUNT;
    return strcmp(argv[2], DIRECTORY_UTF8) == 0 && strcmp(argv[3], WIDE_ARGUMENT_UTF8) == 0 ? ARGV_OK
                                                                                            : ARGV_WRONG_BYTES;
}

PTEST(a_non_ascii_argument_arrives_in_argv_as_its_utf8_bytes)
{
    static wchar_t executable[MAX_PATH];
    static wchar_t command_line[COMMAND_LINE_SIZE];
    const DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
    PT_ASSERT(length > 0 && length < MAX_PATH);
    command_line[0] = L'\0';
    PT_ASSERT(append(command_line, COMMAND_LINE_SIZE, L"\"") && append(command_line, COMMAND_LINE_SIZE, executable)
              && append(command_line, COMMAND_LINE_SIZE, L"\" " ARGV_CHILD_MODE_UTF16 L" " DIRECTORY_UTF16 L" \"")
              && append(command_line, COMMAND_LINE_SIZE, WIDE_ARGUMENT_UTF16 L"\""));
    STARTUPINFOW startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    PROCESS_INFORMATION child;
    PT_ASSERT(CreateProcessW(executable, command_line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child));
    (void)CloseHandle(child.hThread);
    const bool ended = WaitForSingleObject(child.hProcess, WSUP_CHILD_MILLISECONDS) == WAIT_OBJECT_0;
    if (!ended)
        (void)TerminateProcess(child.hProcess, 1);
    DWORD code = 0;
    const bool read = GetExitCodeProcess(child.hProcess, &code);
    (void)CloseHandle(child.hProcess);
    PT_ASSERT(ended && read);
    PT_ASSERT_EQ_U32(code, ARGV_OK);
}

/* True if a file is at the UTF-8 path base, then the wide rest, as the wide
 * API names it. */
static bool exists_as(const char *base, const wchar_t *rest)
{
    static wchar_t path[SAVE_FILE_PATH_SIZE];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, base, -1, path, SAVE_FILE_PATH_SIZE) <= 0)
        return false;
    return append(path, SAVE_FILE_PATH_SIZE, rest) && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

PTEST(a_save_through_a_non_ascii_path_loads_back_under_that_name)
{
    static char base[SAVE_FILE_PATH_SIZE], directory[SAVE_FILE_PATH_SIZE], path[SAVE_FILE_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_directory(base, sizeof base, "test_utf8_windows"));
    /* From here every path reaches the removal below. */
    const bool named = format_path(directory, base, "\\" DIRECTORY_UTF8)
                       && format_path(path, directory, "\\" FILE_UTF8);
    const bool made = named && host_file_make_directory(directory) == 0;
    set_image(0x2A);
    const int init = made ? save_file_init(path) : -99;
    const int store = init == SAVE_FILE_OK ? save_file_store() : -99;
    memset(flash_image(), 0x00, CHIP_BYTES);
    const int load = store == SAVE_FILE_OK ? save_file_load() : -99;
    const bool round_trip = image_is_expected();
    save_file_shutdown();
    const bool on_disk = exists_as(base, L"\\" DIRECTORY_UTF16 L"\\" FILE_UTF16);
    const bool removed = tsup_remove_tree(base);
    PT_ASSERT(named && made);
    PT_ASSERT_EQ_INT(init, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(store, SAVE_FILE_OK);
    PT_ASSERT_EQ_INT(load, SAVE_FILE_OK);
    PT_ASSERT(round_trip);
    PT_ASSERT(on_disk);
    PT_ASSERT(removed);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], ARGV_CHILD_MODE) == 0)
        return check_arguments(argc, argv);
    return ptest_main_for(&ptest_tests, stdout);
}
