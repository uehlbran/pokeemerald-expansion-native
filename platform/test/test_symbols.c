/*
 * test_symbols.c: function names from an ELF symbol table (symbols.c). The
 * test binary is the ELF under test: it is linked without stripping, so its
 * own non-inlined functions are in its .symtab. Damaged files are made by
 * cutting or editing a copy of it.
 */
#include "ptest.h"
#include "symbols.h"

#include <elf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Named, global and never inlined, so that they have a symbol of their own. */
__attribute__((noinline)) int symbols_test_function_alpha(int x)
{
    return x + 1;
}

__attribute__((noinline)) int symbols_test_function_beta(int x)
{
    return x * 3;
}

static uintptr_t address_of(int (*function)(int))
{
    return (uintptr_t)function;
}

PTEST(finds_functions_of_the_running_executable_by_address)
{
    PT_ASSERT_EQ_INT(symbols_load(SYMBOLS_EXE_PATH), 0);
    PT_ASSERT_EQ_STR(symbols_name_at(address_of(symbols_test_function_alpha)), "symbols_test_function_alpha");
    PT_ASSERT_EQ_STR(symbols_name_at(address_of(symbols_test_function_beta)), "symbols_test_function_beta");
    PT_ASSERT(symbols_count() > 2);
    symbols_unload();
}

PTEST(an_address_inside_a_function_or_nowhere_has_no_name)
{
    PT_ASSERT_EQ_INT(symbols_load(SYMBOLS_EXE_PATH), 0);
    PT_ASSERT(symbols_name_at(address_of(symbols_test_function_alpha) + 1) == NULL);
    PT_ASSERT(symbols_name_at(0) == NULL);
    PT_ASSERT(symbols_name_at(UINTPTR_MAX) == NULL);
    symbols_unload();
}

PTEST(nothing_loaded_names_nothing_and_unload_is_repeatable)
{
    symbols_unload();
    symbols_unload();
    PT_ASSERT(symbols_name_at(address_of(symbols_test_function_alpha)) == NULL);
    PT_ASSERT_EQ_U32(symbols_count(), 0);
}

PTEST(loading_twice_replaces_without_leaking)
{
    PT_ASSERT_EQ_INT(symbols_load(SYMBOLS_EXE_PATH), 0);
    uint32_t first = symbols_count();
    PT_ASSERT_EQ_INT(symbols_load(SYMBOLS_EXE_PATH), 0);
    PT_ASSERT_EQ_U32(symbols_count(), first);
    symbols_unload();
}

PTEST(a_missing_file_is_an_error_and_unloads)
{
    PT_ASSERT_EQ_INT(symbols_load(SYMBOLS_EXE_PATH), 0);
    PT_ASSERT_EQ_INT(symbols_load("/nonexistent/for/test"), -1);
    PT_ASSERT_EQ_U32(symbols_count(), 0);
}

/* Writes the first `keep` bytes of this executable, with `patch` applied, to
 * a temporary file whose path goes in `path`. Returns false on any error. */
static bool damaged_copy(char *path, size_t path_size, size_t keep, void (*patch)(unsigned char *bytes, size_t size))
{
    FILE *source = fopen(SYMBOLS_EXE_PATH, "rb");
    if (source == NULL)
        return false;
    unsigned char *bytes = malloc(keep);
    size_t got = bytes != NULL ? fread(bytes, 1, keep, source) : 0;
    fclose(source);
    if (bytes == NULL || got != keep) {
        free(bytes);
        return false;
    }
    if (patch != NULL)
        patch(bytes, keep);
    snprintf(path, path_size, "/tmp/test_symbols_XXXXXX");
    int fd = mkstemp(path);
    bool ok = fd >= 0 && write(fd, bytes, keep) == (ssize_t)keep;
    if (fd >= 0)
        close(fd);
    free(bytes);
    return ok;
}

static void break_magic(unsigned char *bytes, size_t size)
{
    (void)size;
    bytes[0] = 'X';
}

static void break_class(unsigned char *bytes, size_t size)
{
    (void)size;
    bytes[EI_CLASS] = bytes[EI_CLASS] == ELFCLASS32 ? ELFCLASS64 : ELFCLASS32;
}

static int load_damaged(size_t keep, void (*patch)(unsigned char *, size_t))
{
    char path[64];
    if (!damaged_copy(path, sizeof path, keep, patch))
        return 99;
    int result = symbols_load(path);
    unlink(path);
    return result;
}

PTEST(rejects_a_file_that_is_not_elf)
{
    PT_ASSERT_EQ_INT(load_damaged(4096, break_magic), -1);
}

PTEST(rejects_an_elf_of_the_other_width)
{
    PT_ASSERT_EQ_INT(load_damaged(4096, break_class), -1);
}

PTEST(rejects_a_truncated_file_and_an_empty_one)
{
    PT_ASSERT_EQ_INT(load_damaged(10, NULL), -1);   /* shorter than the header */
    PT_ASSERT_EQ_INT(load_damaged(4096, NULL), -1); /* the section headers are past the end */
    PT_ASSERT_EQ_INT(load_damaged(1, NULL), -1);
}

PTEST_MAIN()
