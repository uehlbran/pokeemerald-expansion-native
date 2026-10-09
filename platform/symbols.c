/*
 * symbols.c: function names from the executable's ELF symbol table
 * (symbols.h).
 *
 * Followed: the System V ABI / ELF specification (elf(5), <elf.h>): the file
 * header's section header table, the SHT_SYMTAB section and the string table
 * it links to. Every offset and size read from the file is checked against
 * the file's length before it is trusted; a bad file is an error, never an
 * out-of-range read.
 */
#include "symbols.h"

#include <elf.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if UINTPTR_MAX == UINT64_MAX
#define SYMBOLS_ELF_CLASS ELFCLASS64
typedef Elf64_Ehdr elf_header;
typedef Elf64_Shdr elf_section;
typedef Elf64_Sym elf_symbol;
#else
#define SYMBOLS_ELF_CLASS ELFCLASS32
typedef Elf32_Ehdr elf_header;
typedef Elf32_Shdr elf_section;
typedef Elf32_Sym elf_symbol;
#endif

struct symbol {
    uintptr_t address;
    uint32_t name_offset; /* into names */
};

static struct {
    struct symbol *symbols; /* sorted by address */
    uint32_t count;
    char *names; /* the string table, NUL-terminated at its end */
    size_t names_size;
} loaded;

void symbols_unload(void)
{
    free(loaded.symbols);
    free(loaded.names);
    loaded.symbols = NULL;
    loaded.names = NULL;
    loaded.count = 0;
    loaded.names_size = 0;
}

uint32_t symbols_count(void)
{
    return loaded.count;
}

static int by_address(const void *a, const void *b)
{
    const struct symbol *x = a;
    const struct symbol *y = b;
    return (x->address > y->address) - (x->address < y->address);
}

/* A range of the file that is inside it. */
static bool in_file(uint64_t offset, uint64_t size, uint64_t file_size)
{
    return offset <= file_size && size <= file_size - offset;
}

static bool read_at(FILE *file, uint64_t offset, void *out, size_t size)
{
    return offset <= (uint64_t)LONG_MAX && fseek(file, (long)offset, SEEK_SET) == 0
           && fread(out, 1, size, file) == size;
}

static bool read_header(FILE *file, uint64_t file_size, elf_header *header)
{
    if (!in_file(0, sizeof *header, file_size) || !read_at(file, 0, header, sizeof *header))
        return false;
    if (memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 || header->e_ident[EI_CLASS] != SYMBOLS_ELF_CLASS
        || header->e_shentsize != sizeof(elf_section))
        return false;
    if (header->e_type != ET_EXEC) {
        /* A PIE (ET_DYN) has load-bias-relative symbol values; not supported. */
        (void)fprintf(stderr, "symbols: the executable is not ET_EXEC (a PIE?); the native build links with -no-pie\n");
        return false;
    }
    return true;
}

/* Reads section `index` of the table at the header. */
static bool read_section(FILE *file, uint64_t file_size, const elf_header *header, uint32_t index, elf_section *out)
{
    uint64_t offset = (uint64_t)header->e_shoff + (uint64_t)index * sizeof *out;
    return index < header->e_shnum && in_file(offset, sizeof *out, file_size)
           && read_at(file, offset, out, sizeof *out);
}

/* Finds the SHT_SYMTAB section and its string table. */
static bool find_symtab(FILE *file, uint64_t file_size, const elf_header *header, elf_section *symtab,
                        elf_section *strtab)
{
    for (uint32_t i = 0; i < header->e_shnum; i++) {
        if (!read_section(file, file_size, header, i, symtab))
            return false;
        if (symtab->sh_type == SHT_SYMTAB)
            return symtab->sh_entsize == sizeof(elf_symbol)
                   && read_section(file, file_size, header, symtab->sh_link, strtab) && strtab->sh_type == SHT_STRTAB;
    }
    return false;
}

static bool load_names(FILE *file, uint64_t file_size, const elf_section *strtab)
{
    if (strtab->sh_size == 0 || !in_file(strtab->sh_offset, strtab->sh_size, file_size)
        || strtab->sh_size >= UINT32_MAX)
        return false;
    loaded.names = malloc((size_t)strtab->sh_size + 1u);
    if (loaded.names == NULL)
        return false;
    loaded.names_size = (size_t)strtab->sh_size + 1u;
    if (!read_at(file, strtab->sh_offset, loaded.names, (size_t)strtab->sh_size))
        return false;
    loaded.names[strtab->sh_size] = '\0';
    return true;
}

static bool load_symbols(FILE *file, uint64_t file_size, const elf_section *symtab)
{
    if (!in_file(symtab->sh_offset, symtab->sh_size, file_size) || symtab->sh_size / sizeof(elf_symbol) >= UINT32_MAX)
        return false;
    size_t total = (size_t)(symtab->sh_size / sizeof(elf_symbol));
    elf_symbol *raw = malloc(symtab->sh_size > 0 ? (size_t)symtab->sh_size : 1u);
    loaded.symbols = malloc((total > 0 ? total : 1u) * sizeof *loaded.symbols);
    bool ok = raw != NULL && loaded.symbols != NULL && read_at(file, symtab->sh_offset, raw, (size_t)symtab->sh_size);
    for (size_t i = 0; ok && i < total; i++) {
        if (ELF32_ST_TYPE(raw[i].st_info) != STT_FUNC || raw[i].st_size == 0 || raw[i].st_name == 0
            || raw[i].st_name >= loaded.names_size)
            continue;
        loaded.symbols[loaded.count++] = (struct symbol){.address = (uintptr_t)raw[i].st_value,
                                                         .name_offset = raw[i].st_name};
    }
    free(raw);
    if (ok)
        qsort(loaded.symbols, loaded.count, sizeof *loaded.symbols, by_address);
    return ok;
}

/* The length of the file, by seeking to its end. */
static bool file_length(FILE *file, uint64_t *length)
{
    if (fseek(file, 0, SEEK_END) != 0)
        return false;
    long end = ftell(file);
    if (end < 0)
        return false;
    *length = (uint64_t)end;
    return true;
}

int symbols_load(const char *path)
{
    symbols_unload();
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        (void)fprintf(stderr, "symbols: cannot open %s\n", path);
        return -1;
    }
    elf_header header;
    elf_section symtab, strtab;
    uint64_t file_size = 0;
    bool ok = file_length(file, &file_size) && read_header(file, file_size, &header)
              && find_symtab(file, file_size, &header, &symtab, &strtab) && load_names(file, file_size, &strtab)
              && load_symbols(file, file_size, &symtab);
    (void)fclose(file);
    if (!ok) {
        (void)fprintf(stderr, "symbols: %s is not an ELF executable of this build's width with a usable symbol table\n",
                      path);
        symbols_unload();
        return -1;
    }
    return 0;
}

const char *symbols_name_at(uintptr_t address)
{
    uint32_t low = 0;
    uint32_t high = loaded.count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (loaded.symbols[middle].address < address)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < loaded.count && loaded.symbols[low].address == address)
        return loaded.names + loaded.symbols[low].name_offset;
    return NULL;
}
