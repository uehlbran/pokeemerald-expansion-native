#include <ctype.h>
#include "elf.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static bool try_patch_value(const char *sym, char *dest, const char *source, size_t size);
static bool patch_pe(char *f, size_t file_size, int argc, char *argv[]);
static bool is_macho(const char *f, size_t file_size);
static bool patch_macho(char *f, size_t file_size, int argc, char *argv[]);

int main(int argc, char *argv[])
{
    int exit_code = 1;

    int fd = -1;
    char *f = MAP_FAILED;

    if (argc < 2 || argc % 2 != 0)
    {
        fprintf(stderr, "Usage: %s <filename> [<symbol> <value>]...\n", argv[0]);
        goto error;
    }

    if ((fd = open(argv[1], O_RDWR)) == -1)
    {
        fprintf(stderr, "open(%s, O_RDWR) failed: %s\n", argv[1], strerror(errno));
        goto error;
    }

    struct stat st;
    if (fstat(fd, &st) == -1)
    {
        perror("stat failed");
        goto error;
    }

    if ((f = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED)
    {
        perror("mmap failed");
        goto error;
    }

    // The native build for Windows is a PE file (patch_pe, below).
    if (st.st_size >= 2 && memcmp(f, "MZ", 2) == 0)
    {
        if (patch_pe(f, st.st_size, argc, argv))
            exit_code = 0;
        goto error;
    }

    // The native build for macOS is a Mach-O file (patch_macho, below).
    if (is_macho(f, st.st_size))
    {
        if (patch_macho(f, st.st_size, argc, argv))
            exit_code = 0;
        goto error;
    }

    if (memcmp(f, ELFMAG, 4) != 0)
    {
        fprintf(stderr, "not an ELF file\n");
        goto error;
    }

    // The ELF class picks the structures: the GBA ROM's ELF and the 32-bit
    // native executable are ELF32, the 64-bit native executable ELF64.
    bool is64;
    if (f[EI_CLASS] == ELFCLASS32)
        is64 = false;
    else if (f[EI_CLASS] == ELFCLASS64)
        is64 = true;
    else
    {
        fprintf(stderr, "unknown ELF class\n");
        goto error;
    }

    const Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)f;
    const Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)f;
    const Elf32_Shdr *shdrs32 = (Elf32_Shdr *)(f + ehdr32->e_shoff);
    const Elf64_Shdr *shdrs64 = (Elf64_Shdr *)(f + ehdr64->e_shoff);
    unsigned shstrndx = is64 ? ehdr64->e_shstrndx : ehdr32->e_shstrndx;
    unsigned shnum = is64 ? ehdr64->e_shnum : ehdr32->e_shnum;

    if (shstrndx == SHN_UNDEF)
    {
        fprintf(stderr, "no section name string table\n");
        goto error;
    }
    const char *shstr = (const char *)(f + (is64 ? shdrs64[shstrndx].sh_offset : shdrs32[shstrndx].sh_offset));
    int symtab_i = -1;
    int strtab_i = -1;
    for (int i = 0; i < shnum; i++)
    {
        const char *sh_name = shstr + (is64 ? shdrs64[i].sh_name : shdrs32[i].sh_name);
        if (strcmp(sh_name, ".symtab") == 0)
            symtab_i = i;
        else if (strcmp(sh_name, ".strtab") == 0)
            strtab_i = i;
    }
    if (symtab_i < 0)
    {
        fprintf(stderr, "no .symtab section\n");
        goto error;
    }
    if (strtab_i < 0)
    {
        fprintf(stderr, "no .strtab section\n");
        goto error;
    }

    size_t symtab_offset = is64 ? shdrs64[symtab_i].sh_offset : shdrs32[symtab_i].sh_offset;
    size_t symtab_size = is64 ? shdrs64[symtab_i].sh_size : shdrs32[symtab_i].sh_size;
    size_t symtab_entsize = is64 ? shdrs64[symtab_i].sh_entsize : shdrs32[symtab_i].sh_entsize;
    const Elf32_Sym *symtab32 = (Elf32_Sym *)(f + symtab_offset);
    const Elf64_Sym *symtab64 = (Elf64_Sym *)(f + symtab_offset);
    const char *strtab = (const char *)(f + (is64 ? shdrs64[strtab_i].sh_offset : shdrs32[strtab_i].sh_offset));
    for (int i = 0; i < symtab_size / symtab_entsize; i++)
    {
        unsigned st_name = is64 ? symtab64[i].st_name : symtab32[i].st_name;
        unsigned st_shndx = is64 ? symtab64[i].st_shndx : symtab32[i].st_shndx;
        if (st_name == 0) continue;
        if (st_shndx > shnum) continue;
        const char *sym_name = strtab + st_name;
        size_t sh_offset, sym_offset, st_size;
        if (is64)
        {
            sh_offset = shdrs64[st_shndx].sh_offset;
            sym_offset = symtab64[i].st_value - shdrs64[st_shndx].sh_addr;
            st_size = symtab64[i].st_size;
        }
        else
        {
            sh_offset = shdrs32[st_shndx].sh_offset;
            sym_offset = (uint32_t)(symtab32[i].st_value - shdrs32[st_shndx].sh_addr);
            st_size = symtab32[i].st_size;
        }
        for (int j = 2; j < argc; j += 2)
        {
            const char *arg_name = argv[j + 0];
            const char *arg_value = argv[j + 1];
            if (strcmp(sym_name, arg_name) == 0)
            {
                char *value = (char *)(f + sh_offset + sym_offset);
                if (!try_patch_value(sym_name, value, arg_value, st_size))
                    goto error;
            }
        }
    }

    exit_code = 0;

error:
    if (f != MAP_FAILED)
    {
        if (msync(f, st.st_size, MS_SYNC) == -1)
        {
            perror("msync failed");
            f = MAP_FAILED;
        }
    }
    if (f != MAP_FAILED)
    {
        if (munmap(f, st.st_size) == -1)
        {
            perror("munmap failed");
        }
    }
    if (fd != -1)
    {
        if (close(fd) == -1)
        {
            perror("close failed");
        }
    }

    return exit_code;
}

static int parsexdigit(char c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    else if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    else if ('A' <= c && c <= 'F')
        return c - 'A' + 10;
    else
        return -1;
}

static bool try_patch_value(const char *sym, char *dest, const char *source, size_t size)
{
    int i = 0;
    while (*source)
    {
        if (i == size)
        {
            fprintf(stderr, "%s: overflows size (%lu)\n", sym, size);
            return false;
        }
        char c, value;
        switch ((c = *source++))
        {
        case '\\':
            switch ((c = *source++))
            {
            case '0':
                value = 0;
                break;
            case 'x':
                if (!isxdigit((c = *source++)))
                {
                    fprintf(stderr, "%s: illegal escape \\x%c\n", sym, c);
                    return false;
                }
                value = parsexdigit(c);
                if (!isxdigit((c = *source++)))
                {
                    fprintf(stderr, "%s: illegal escape \\x%c%c\n", sym, *(source - 2), c);
                    return false;
                }
                value = value * 16 + parsexdigit(c);
                break;
            default:
                fprintf(stderr, "%s: illegal escape \\%c\n", sym, c);
                return false;
            }
            break;
        default:
            value = c;
            break;
        }
        dest[i++] = value;
    }
    return true;
}

// A Windows executable, as GNU ld writes one for MinGW-w64, keeps a COFF
// symbol table (Microsoft's "PE Format", sections "COFF File Header",
// "Section Table" and "COFF Symbol Table"): each symbol names its section,
// 1-based, and in an image its value is the offset in that section; the
// section's header says where its bytes are in the file. COFF records no
// sizes, so a value may fill its symbol up to the next symbol of the same
// section, or to the end of the section's bytes in the file. As on ELF, every
// symbol of a given name is patched, and a name that is not there is not an
// error.
#define DOS_HEADER_SIZE 0x40
#define DOS_PE_HEADER_OFFSET_AT 0x3C
#define PE_SIGNATURE_SIZE 4
#define COFF_HEADER_SIZE 20
#define COFF_SECTION_COUNT_AT 2
#define COFF_SYMBOL_TABLE_AT 8
#define COFF_SYMBOL_COUNT_AT 12
#define COFF_OPTIONAL_HEADER_SIZE_AT 16
#define SECTION_HEADER_SIZE 40
#define SECTION_VIRTUAL_SIZE_AT 8
#define SECTION_RAW_SIZE_AT 16
#define SECTION_RAW_OFFSET_AT 20
#define SYMBOL_SIZE 18
#define SYMBOL_SHORT_NAME_SIZE 8
#define SYMBOL_NAME_OFFSET_AT 4
#define SYMBOL_VALUE_AT 8
#define SYMBOL_SECTION_AT 12
#define SYMBOL_AUX_COUNT_AT 17
#define STRING_TABLE_SIZE_SIZE 4

struct CoffSymbols
{
    const char *records;
    uint32_t count;
    const char *strings;
    uint32_t strings_size;
};

// Little-endian, of 2 or 4 bytes.
static uint32_t read_le(const char *p, int size)
{
    uint32_t value = 0;
    for (int i = size - 1; i >= 0; i--)
        value = (value << 8) | (unsigned char)p[i];
    return value;
}

// A name of up to 8 bytes is in the record, NUL-padded; a longer one is in
// the string table that follows the records.
static bool coff_symbol_is(const struct CoffSymbols *symbols, const char *record, const char *name)
{
    size_t length = strlen(name);
    if (read_le(record, 4) != 0)
        return length <= SYMBOL_SHORT_NAME_SIZE && memcmp(record, name, length) == 0
            && (length == SYMBOL_SHORT_NAME_SIZE || record[length] == '\0');
    uint32_t offset = read_le(record + SYMBOL_NAME_OFFSET_AT, 4);
    return offset < symbols->strings_size && length < symbols->strings_size - offset
        && memcmp(symbols->strings + offset, name, length + 1) == 0;
}

// The bytes from `value` to the next symbol of `section`, or to `end`.
static size_t coff_symbol_room(const struct CoffSymbols *symbols, uint32_t section, uint32_t value, uint32_t end)
{
    uint32_t next = end;
    for (uint32_t i = 0; i < symbols->count; i += 1 + (unsigned char)symbols->records[(size_t)i * SYMBOL_SIZE + SYMBOL_AUX_COUNT_AT])
    {
        const char *record = symbols->records + (size_t)i * SYMBOL_SIZE;
        uint32_t other = read_le(record + SYMBOL_VALUE_AT, 4);
        if (read_le(record + SYMBOL_SECTION_AT, 2) == section && other > value && other < next)
            next = other;
    }
    return next - value;
}

static bool patch_pe(char *f, size_t file_size, int argc, char *argv[])
{
    if (file_size < DOS_HEADER_SIZE)
    {
        fprintf(stderr, "not a PE file\n");
        return false;
    }
    size_t pe = read_le(f + DOS_PE_HEADER_OFFSET_AT, 4);
    if (pe > file_size - PE_SIGNATURE_SIZE - COFF_HEADER_SIZE || memcmp(f + pe, "PE\0\0", PE_SIGNATURE_SIZE) != 0)
    {
        fprintf(stderr, "not a PE file\n");
        return false;
    }
    const char *coff = f + pe + PE_SIGNATURE_SIZE;
    uint32_t section_count = read_le(coff + COFF_SECTION_COUNT_AT, 2);
    size_t sections_offset = pe + PE_SIGNATURE_SIZE + COFF_HEADER_SIZE + read_le(coff + COFF_OPTIONAL_HEADER_SIZE_AT, 2);
    if (sections_offset + (size_t)section_count * SECTION_HEADER_SIZE > file_size)
    {
        fprintf(stderr, "the section table is outside the file\n");
        return false;
    }
    size_t symbols_offset = read_le(coff + COFF_SYMBOL_TABLE_AT, 4);
    uint32_t symbol_count = read_le(coff + COFF_SYMBOL_COUNT_AT, 4);
    if (symbols_offset == 0 || symbol_count == 0)
    {
        fprintf(stderr, "no COFF symbol table\n");
        return false;
    }
    size_t strings_offset = symbols_offset + (size_t)symbol_count * SYMBOL_SIZE;
    if (strings_offset > file_size - STRING_TABLE_SIZE_SIZE)
    {
        fprintf(stderr, "the COFF symbol table is outside the file\n");
        return false;
    }
    uint32_t strings_size = read_le(f + strings_offset, 4);
    if (strings_size < STRING_TABLE_SIZE_SIZE || strings_size > file_size - strings_offset)
    {
        fprintf(stderr, "the COFF string table is outside the file\n");
        return false;
    }
    const struct CoffSymbols symbols = { f + symbols_offset, symbol_count, f + strings_offset, strings_size };

    for (uint32_t i = 0; i < symbol_count; i += 1 + (unsigned char)symbols.records[(size_t)i * SYMBOL_SIZE + SYMBOL_AUX_COUNT_AT])
    {
        const char *record = symbols.records + (size_t)i * SYMBOL_SIZE;
        // 0 is undefined; 0xFFFF (absolute) and 0xFFFE (debugging) are above any count.
        uint32_t section = read_le(record + SYMBOL_SECTION_AT, 2);
        if (section == 0 || section > section_count)
            continue;
        for (int j = 2; j < argc; j += 2)
        {
            if (!coff_symbol_is(&symbols, record, argv[j]))
                continue;
            const char *header = f + sections_offset + (size_t)(section - 1) * SECTION_HEADER_SIZE;
            uint32_t virtual_size = read_le(header + SECTION_VIRTUAL_SIZE_AT, 4);
            uint32_t raw_size = read_le(header + SECTION_RAW_SIZE_AT, 4);
            uint32_t end = virtual_size < raw_size ? virtual_size : raw_size;
            size_t raw_offset = read_le(header + SECTION_RAW_OFFSET_AT, 4);
            uint32_t value = read_le(record + SYMBOL_VALUE_AT, 4);
            if (raw_offset > file_size || end > file_size - raw_offset || value >= end)
            {
                fprintf(stderr, "%s: not stored in the file\n", argv[j]);
                return false;
            }
            size_t room = coff_symbol_room(&symbols, section, value, end);
            if (!try_patch_value(argv[j], f + raw_offset + value, argv[j + 1], room))
                return false;
        }
    }
    return true;
}

// A macOS executable (or object), as Apple's ld64 writes one for x86_64: a
// 64-bit little-endian Mach-O file (<mach-o/loader.h>, <mach-o/nlist.h>).
// The LC_SEGMENT_64 load commands list the sections, each with its address
// and where its bytes are in the file; LC_SYMTAB gives the symbol table,
// whose N_SECT symbols name their section, 1-based in the order the segments
// list them, and their address. A C name has Mach-O's leading underscore, so
// the symbol "_gTestRunnerN" is the argument "gTestRunnerN". Mach-O records
// no sizes, so a value may fill its symbol up to the next symbol of the same
// section, or to the section's end, as on PE. A zero-filled section has no
// bytes in the file. As on ELF and PE, every symbol of a given name is
// patched, and a name that is not there is not an error.
#define MH_MAGIC_64 0xFEEDFACFu
#define MACH_HEADER_SIZE 32
#define MACH_COMMAND_COUNT_AT 16
#define MACH_COMMANDS_SIZE_AT 20
#define LOAD_COMMAND_SIZE_AT 4
#define LOAD_COMMAND_MIN_SIZE 8
#define LC_SYMTAB 0x2u
#define LC_SEGMENT_64 0x19u
#define SEGMENT_HEADER_SIZE 72
#define SEGMENT_SECTION_COUNT_AT 64
#define MACH_SECTION_SIZE 80
#define MACH_SECTION_ADDR_AT 32
#define MACH_SECTION_SIZE_AT 40
#define MACH_SECTION_OFFSET_AT 48
#define MACH_SECTION_FLAGS_AT 64
#define MACH_SECTION_TYPE_MASK 0xFFu
#define S_ZEROFILL 0x1u
#define S_GB_ZEROFILL 0xCu
#define S_THREAD_LOCAL_ZEROFILL 0x12u
#define SYMTAB_OFFSET_AT 8
#define SYMTAB_COUNT_AT 12
#define STRTAB_OFFSET_AT 16
#define STRTAB_SIZE_AT 20
#define SYMTAB_COMMAND_SIZE 24
#define NLIST_SIZE 16
#define NLIST_TYPE_AT 4
#define NLIST_SECTION_AT 5
#define NLIST_VALUE_AT 8
#define N_STAB 0xE0u
#define N_TYPE 0x0Eu
#define N_SECT 0x0Eu
#define MACH_SECTIONS_MAX 255   // n_sect is one byte; 0 is NO_SECT

struct MachSection
{
    uint64_t addr;
    uint64_t size;
    uint32_t offset;   // 0: no bytes in the file
};

static uint64_t read_le64(const char *p)
{
    return read_le(p, 4) | (uint64_t)read_le(p + 4, 4) << 32;
}

static bool is_macho(const char *f, size_t file_size)
{
    return file_size >= MACH_HEADER_SIZE && read_le(f, 4) == MH_MAGIC_64;
}

// The bytes from `value` to the next N_SECT symbol of `section`, or to `end`.
static uint64_t macho_symbol_room(const char *symbols, uint32_t count, unsigned section, uint64_t value, uint64_t end)
{
    uint64_t next = end;
    for (uint32_t i = 0; i < count; i++)
    {
        const char *record = symbols + (size_t)i * NLIST_SIZE;
        unsigned type = (unsigned char)record[NLIST_TYPE_AT];
        uint64_t other = read_le64(record + NLIST_VALUE_AT);
        if (!(type & N_STAB) && (type & N_TYPE) == N_SECT && (unsigned char)record[NLIST_SECTION_AT] == section
            && other > value && other < next)
            next = other;
    }
    return next - value;
}

static bool patch_macho(char *f, size_t file_size, int argc, char *argv[])
{
    struct MachSection sections[MACH_SECTIONS_MAX];
    unsigned section_count = 0;
    const char *symtab = NULL;
    uint32_t command_count = read_le(f + MACH_COMMAND_COUNT_AT, 4);
    size_t commands_end = MACH_HEADER_SIZE + (size_t)read_le(f + MACH_COMMANDS_SIZE_AT, 4);
    if (commands_end > file_size)
    {
        fprintf(stderr, "the load commands are outside the file\n");
        return false;
    }
    size_t at = MACH_HEADER_SIZE;
    for (uint32_t i = 0; i < command_count; i++)
    {
        if (at > commands_end - LOAD_COMMAND_MIN_SIZE)
        {
            fprintf(stderr, "a load command is outside the load commands\n");
            return false;
        }
        uint32_t command = read_le(f + at, 4);
        uint32_t command_size = read_le(f + at + LOAD_COMMAND_SIZE_AT, 4);
        if (command_size < LOAD_COMMAND_MIN_SIZE || command_size > commands_end - at)
        {
            fprintf(stderr, "a load command is outside the load commands\n");
            return false;
        }
        if (command == LC_SEGMENT_64)
        {
            uint32_t nsects = command_size >= SEGMENT_HEADER_SIZE ? read_le(f + at + SEGMENT_SECTION_COUNT_AT, 4) : 0;
            if (command_size < SEGMENT_HEADER_SIZE || nsects > (command_size - SEGMENT_HEADER_SIZE) / MACH_SECTION_SIZE)
            {
                fprintf(stderr, "a segment's sections are outside its load command\n");
                return false;
            }
            if (nsects > MACH_SECTIONS_MAX - section_count)
            {
                fprintf(stderr, "more than %d sections\n", MACH_SECTIONS_MAX);
                return false;
            }
            for (uint32_t j = 0; j < nsects; j++)
            {
                const char *header = f + at + SEGMENT_HEADER_SIZE + (size_t)j * MACH_SECTION_SIZE;
                uint32_t type = read_le(header + MACH_SECTION_FLAGS_AT, 4) & MACH_SECTION_TYPE_MASK;
                bool zerofill = type == S_ZEROFILL || type == S_GB_ZEROFILL || type == S_THREAD_LOCAL_ZEROFILL;
                sections[section_count].addr = read_le64(header + MACH_SECTION_ADDR_AT);
                sections[section_count].size = read_le64(header + MACH_SECTION_SIZE_AT);
                sections[section_count].offset = zerofill ? 0 : read_le(header + MACH_SECTION_OFFSET_AT, 4);
                section_count++;
            }
        }
        else if (command == LC_SYMTAB)
        {
            if (command_size < SYMTAB_COMMAND_SIZE)
            {
                fprintf(stderr, "LC_SYMTAB is too short\n");
                return false;
            }
            symtab = f + at;
        }
        at += command_size;
    }
    if (symtab == NULL)
    {
        fprintf(stderr, "no Mach-O symbol table\n");
        return false;
    }
    size_t symbols_offset = read_le(symtab + SYMTAB_OFFSET_AT, 4);
    uint32_t symbol_count = read_le(symtab + SYMTAB_COUNT_AT, 4);
    size_t strings_offset = read_le(symtab + STRTAB_OFFSET_AT, 4);
    uint32_t strings_size = read_le(symtab + STRTAB_SIZE_AT, 4);
    if (symbols_offset > file_size || symbol_count > (file_size - symbols_offset) / NLIST_SIZE
        || strings_offset > file_size || strings_size > file_size - strings_offset)
    {
        fprintf(stderr, "the Mach-O symbol table is outside the file\n");
        return false;
    }
    const char *symbols = f + symbols_offset;
    const char *strings = f + strings_offset;

    for (uint32_t i = 0; i < symbol_count; i++)
    {
        const char *record = symbols + (size_t)i * NLIST_SIZE;
        unsigned type = (unsigned char)record[NLIST_TYPE_AT];
        unsigned section = (unsigned char)record[NLIST_SECTION_AT];
        uint32_t name_offset = read_le(record, 4);
        if ((type & N_STAB) || (type & N_TYPE) != N_SECT || section == 0 || section > section_count
            || name_offset >= strings_size || strings[name_offset] != '_')
            continue;
        const char *name = strings + name_offset + 1;
        size_t name_room = strings_size - name_offset - 1;
        for (int j = 2; j < argc; j += 2)
        {
            size_t length = strlen(argv[j]);
            if (length >= name_room || memcmp(name, argv[j], length + 1) != 0)
                continue;
            const struct MachSection *where = &sections[section - 1];
            uint64_t value = read_le64(record + NLIST_VALUE_AT);
            if (where->offset == 0 || where->offset > file_size || where->size > file_size - where->offset
                || value < where->addr || value - where->addr >= where->size)
            {
                fprintf(stderr, "%s: not stored in the file\n", argv[j]);
                return false;
            }
            uint64_t room = macho_symbol_room(symbols, symbol_count, section, value, where->addr + where->size);
            if (!try_patch_value(argv[j], f + where->offset + (value - where->addr), argv[j + 1], room))
                return false;
        }
    }
    return true;
}
