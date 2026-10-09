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
