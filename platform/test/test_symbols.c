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

/* --- Lying headers, in a whole copy of this executable ------------------------- */

#include <link.h>
#include <sys/stat.h>

enum lie {
    LIE_SH_SIZE,       /* the symbol table is longer than the file */
    LIE_SH_OFFSET,     /* the symbol table starts past the end of the file */
    LIE_SH_LINK,       /* its string table is a section that does not exist */
    LIE_SH_LINK_WRONG, /* its string table is a section that is not one */
    LIE_SH_ENTSIZE,    /* its entries are not symbols */
    LIE_E_SHNUM,       /* a section count that leaves the symbol table out */
    LIE_STRTAB_SMALL,  /* the string table is shorter than the names point to */
    LIE_E_TYPE         /* a shared object, as a PIE is */
};

/* Applies the lie to the bytes of the executable. */
static void tell(unsigned char *bytes, enum lie lie)
{
    ElfW(Ehdr) eh;
    memcpy(&eh, bytes, sizeof eh);
    ElfW(Shdr) *sections = (ElfW(Shdr) *)(void *)(bytes + eh.e_shoff);
    ElfW(Shdr) *symtab = NULL;
    for (size_t i = 0; i < eh.e_shnum; i++) {
        if (sections[i].sh_type == SHT_SYMTAB)
            symtab = &sections[i];
    }
    if (symtab == NULL)
        return;
    switch (lie) {
    case LIE_SH_SIZE:
        symtab->sh_size = (ElfW(Xword))0x7FFFFFF0u;
        break;
    case LIE_SH_OFFSET:
        symtab->sh_offset = (ElfW(Off))0x7FFFFFF0u;
        break;
    case LIE_SH_LINK:
        symtab->sh_link = eh.e_shnum + 5u;
        break;
    case LIE_SH_LINK_WRONG:
        symtab->sh_link = 0; /* the null section, not a string table */
        break;
    case LIE_SH_ENTSIZE:
        symtab->sh_entsize = 3;
        break;
    case LIE_E_SHNUM:
        eh.e_shnum = 1; /* the symbol table is not among the sections the header admits to */
        memcpy(bytes, &eh, sizeof eh);
        break;
    case LIE_STRTAB_SMALL:
        sections[symtab->sh_link].sh_size = 4;
        break;
    case LIE_E_TYPE:
        eh.e_type = ET_DYN;
        memcpy(bytes, &eh, sizeof eh);
        break;
    }
}

static int load_lying(enum lie lie)
{
    struct stat st;
    FILE *source = fopen(SYMBOLS_EXE_PATH, "rb");
    if (source == NULL || fstat(fileno(source), &st) != 0)
        return 99;
    unsigned char *bytes = malloc((size_t)st.st_size);
    size_t got = bytes != NULL ? fread(bytes, 1, (size_t)st.st_size, source) : 0;
    fclose(source);
    if (bytes == NULL || got != (size_t)st.st_size) {
        free(bytes);
        return 99;
    }
    tell(bytes, lie);
    char path[64];
    snprintf(path, sizeof path, "/tmp/test_symbols_XXXXXX");
    int fd = mkstemp(path);
    bool ok = fd >= 0 && write(fd, bytes, got) == (ssize_t)got;
    if (fd >= 0)
        close(fd);
    free(bytes);
    if (!ok)
        return 99;
    int result = symbols_load(path);
    unlink(path);
    return result;
}

PTEST(a_symbol_table_longer_than_the_file_is_refused)
{
    PT_ASSERT_EQ_INT(load_lying(LIE_SH_SIZE), -1);
}

PTEST(a_symbol_table_past_the_end_of_the_file_is_refused)
{
    PT_ASSERT_EQ_INT(load_lying(LIE_SH_OFFSET), -1);
}

PTEST(a_string_table_link_that_does_not_exist_or_is_not_a_string_table_is_refused)
{
    PT_ASSERT_EQ_INT(load_lying(LIE_SH_LINK), -1);
    PT_ASSERT_EQ_INT(load_lying(LIE_SH_LINK_WRONG), -1);
}

PTEST(entries_that_are_not_symbols_are_refused)
{
    PT_ASSERT_EQ_INT(load_lying(LIE_SH_ENTSIZE), -1);
}

PTEST(a_section_count_that_hides_the_symbol_table_is_refused)
{
    PT_ASSERT_EQ_INT(load_lying(LIE_E_SHNUM), -1);
}

PTEST(a_name_past_the_end_of_the_string_table_is_skipped_not_read)
{
    /* the symbols load; none can have a name inside a 4-byte table, and nothing is read past it */
    int result = load_lying(LIE_STRTAB_SMALL);
    PT_ASSERT(result == 0 || result == -1);
    PT_ASSERT(symbols_name_at((uintptr_t)symbols_test_function_alpha) == NULL);
    symbols_unload();
}

PTEST(a_position_independent_executable_is_refused)
{
    PT_ASSERT_EQ_INT(load_lying(LIE_E_TYPE), -1);
}

PTEST_MAIN()
