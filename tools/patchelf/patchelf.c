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

// The ELF class picks the structures: the GBA ROM's ELF and the 32-bit native
// executable are ELF32, the 64-bit native executable ELF64. These read the
// fields of either with the width of the class folded away.
struct ElfImage
{
    const char *base;
    bool is64;
    unsigned shnum;
    unsigned shstrndx;
};

struct ElfSection
{
    unsigned name;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint64_t entsize;
};

struct ElfSymbol
{
    unsigned name;
    unsigned shndx;
    uint64_t value;
    uint64_t size;
};

static bool read_image(struct ElfImage *image, const char *base)
{
    image->base = base;
    if (base[EI_CLASS] == ELFCLASS32)
        image->is64 = false;
    else if (base[EI_CLASS] == ELFCLASS64)
        image->is64 = true;
    else
        return false;
    image->shnum = image->is64 ? ((const Elf64_Ehdr *)base)->e_shnum : ((const Elf32_Ehdr *)base)->e_shnum;
    image->shstrndx = image->is64 ? ((const Elf64_Ehdr *)base)->e_shstrndx : ((const Elf32_Ehdr *)base)->e_shstrndx;
    return true;
}

static struct ElfSection read_section(const struct ElfImage *image, unsigned index)
{
    struct ElfSection section;
    if (image->is64)
    {
        const Elf64_Shdr *shdr = &((const Elf64_Shdr *)(image->base + ((const Elf64_Ehdr *)image->base)->e_shoff))[index];
        section = (struct ElfSection){ shdr->sh_name, shdr->sh_addr, shdr->sh_offset, shdr->sh_size, shdr->sh_entsize };
    }
    else
    {
        const Elf32_Shdr *shdr = &((const Elf32_Shdr *)(image->base + ((const Elf32_Ehdr *)image->base)->e_shoff))[index];
        section = (struct ElfSection){ shdr->sh_name, shdr->sh_addr, shdr->sh_offset, shdr->sh_size, shdr->sh_entsize };
    }
    return section;
}

static struct ElfSymbol read_symbol(const struct ElfImage *image, const struct ElfSection *symtab, unsigned index)
{
    struct ElfSymbol symbol;
    if (image->is64)
    {
        const Elf64_Sym *sym = &((const Elf64_Sym *)(image->base + symtab->offset))[index];
        symbol = (struct ElfSymbol){ sym->st_name, sym->st_shndx, sym->st_value, sym->st_size };
    }
    else
    {
        const Elf32_Sym *sym = &((const Elf32_Sym *)(image->base + symtab->offset))[index];
        symbol = (struct ElfSymbol){ sym->st_name, sym->st_shndx, sym->st_value, sym->st_size };
    }
    return symbol;
}

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

    if (memcmp(f, ELFMAG, 4) != 0)
    {
        fprintf(stderr, "not an ELF file\n");
        goto error;
    }

    struct ElfImage image;
    if (!read_image(&image, f))
    {
        fprintf(stderr, "unknown ELF class\n");
        goto error;
    }

    if (image.shstrndx == SHN_UNDEF)
    {
        fprintf(stderr, "no section name string table\n");
        goto error;
    }
    const char *shstr = f + read_section(&image, image.shstrndx).offset;
    int symtab_i = -1;
    int strtab_i = -1;
    for (unsigned i = 0; i < image.shnum; i++)
    {
        const char *sh_name = shstr + read_section(&image, i).name;
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

    const struct ElfSection symtab = read_section(&image, symtab_i);
    const char *strtab = f + read_section(&image, strtab_i).offset;
    for (unsigned i = 0; i < symtab.size / symtab.entsize; i++)
    {
        const struct ElfSymbol symbol = read_symbol(&image, &symtab, i);
        if (symbol.name == 0) continue;
        if (symbol.shndx > image.shnum) continue;
        const char *sym_name = strtab + symbol.name;
        const struct ElfSection section = read_section(&image, symbol.shndx);
        // An ELF32 file's addresses wrap at 32 bits.
        size_t sym_offset = image.is64 ? symbol.value - section.addr : (uint32_t)(symbol.value - section.addr);
        for (int j = 2; j < argc; j += 2)
        {
            const char *arg_name = argv[j + 0];
            const char *arg_value = argv[j + 1];
            if (strcmp(sym_name, arg_name) == 0)
            {
                char *value = f + section.offset + sym_offset;
                if (!try_patch_value(sym_name, value, arg_value, symbol.size))
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
