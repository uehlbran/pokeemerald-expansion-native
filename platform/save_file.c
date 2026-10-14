/*
 * save_file.c: the cartridge's persistence, the flash image kept in a
 * versioned .sav file. Interface, file format and how main uses it:
 * save_file.h.
 *
 * Stands in for the flash chip's non-volatility (GBATEK "GBA Cart Backup
 * Flash ROM", https://problemkaputt.de/gbatek.htm#gbacartbackupflashrom:
 * the save survives power-off). Written from POSIX and docs/RESEARCH.md,
 * "Save"; the ports' save code was read for its pitfalls only
 * (docs/research/port-*.md): reads from a second file handle, unflushed
 * writes, no atomic replacement, and regionalidades' rename without fsync.
 *
 * Load: fopen, fstat for the type and size, then the first
 * SAVE_FILE_HEADER_SIZE bytes. With the magic, the header must be complete,
 * of a known version, for a 32- or 64-bit build and a FLASH_CHIP_SIZE image,
 * the file exactly header plus image long, and the image's CRC must match;
 * the image is read straight into the chip and the layout tag kept for
 * save_file_loaded_layout. Without the magic, the file must be a bare image
 * of exactly FLASH_CHIP_SIZE bytes (legacy). Anything else is reported and
 * leaves an erased chip.
 * Store: the header and the image go to <path>.tmp, which is flushed,
 * synced to the disk and closed before it atomically replaces <path>, and
 * then the directory is synced so the replacement itself survives a crash
 * (host_file.h). On any failure before the replacement, the tmp file this
 * store created is removed and <path> is untouched.
 *
 * The header's integers are encoded and decoded a byte at a time as
 * little-endian, so the format does not depend on the host's byte order or
 * struct padding; struct header_bytes is only byte arrays (asserted size and
 * offsets). Module state: the two paths, fixed buffers of SAVE_FILE_PATH_SIZE
 * computed once by save_file_init, and the last loaded layout tag. Nothing
 * is allocated.
 */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "flash.h"
#include "host_file.h"
#include "printf_format.h"
#include "save_file.h"

/* Appended to the save path for the file a store writes before renaming. */
#define SAVE_FILE_TMP_SUFFIX ".tmp"

/* GBATEK "GBA Cart Backup Flash ROM": "64 KBytes - 512Kbits Flash ROM", the
 * smaller chip; a .sav of this size is from a different cartridge type. */
#define FLASH_512K_CHIP_SIZE 0x10000u

/* CRC-32/ISO-HDLC: reflected polynomial 0x04C11DB7, initial value and final
 * XOR all ones. */
#define CRC32_REFLECTED_POLYNOMIAL UINT32_C(0xEDB88320)
#define CRC32_INITIAL UINT32_C(0xFFFFFFFF)
#define CRC32_FINAL_XOR UINT32_C(0xFFFFFFFF)

/* The pointer widths a layout tag may name. */
#define POINTER_BITS_32 32u
#define POINTER_BITS_64 64u

/* The image's game data, unlike the header, is the game's own u16 and u32
 * fields, little-endian on the ARM7TDMI; on a little-endian host they have
 * the same bytes, so images move between the GBA, emulators and this build
 * unchanged. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
               "a .sav image holds little-endian fields; a big-endian host needs the game to swap them");

#define MAGIC_SIZE 8u
static const uint8_t magic[MAGIC_SIZE] = {'P', 'E', 'N', 'S', 'A', 'V', 'E', '\0'};

/* Bytes of each header integer, a u32 stored least significant byte first. */
#define SAVE_FILE_U32_BYTES 4u

/* The header as it is in the file. */
struct header_bytes {
    uint8_t magic[MAGIC_SIZE];
    uint8_t version[SAVE_FILE_U32_BYTES];
    uint8_t pointer_bits[SAVE_FILE_U32_BYTES];
    uint8_t layout_id[SAVE_FILE_U32_BYTES];
    uint8_t image_size[SAVE_FILE_U32_BYTES];
    uint8_t image_crc32[SAVE_FILE_U32_BYTES];
};
_Static_assert(sizeof(struct header_bytes) == SAVE_FILE_HEADER_SIZE, "header size");
// NOLINTBEGIN(readability-magic-numbers): the offsets are the file format's own (save_file.h)
_Static_assert(offsetof(struct header_bytes, version) == 8, "version offset");
_Static_assert(offsetof(struct header_bytes, pointer_bits) == 12, "pointer width offset");
_Static_assert(offsetof(struct header_bytes, layout_id) == 16, "layout id offset");
_Static_assert(offsetof(struct header_bytes, image_size) == 20, "image size offset");
_Static_assert(offsetof(struct header_bytes, image_crc32) == 24, "CRC offset");
// NOLINTEND(readability-magic-numbers)

static struct {
    bool initialised;
    bool persistent; /* false: save_file_init(NULL), in memory only */
    char path[SAVE_FILE_PATH_SIZE];
    char tmp_path[SAVE_FILE_PATH_SIZE]; /* path + SAVE_FILE_TMP_SUFFIX */
    bool has_loaded_layout;             /* the last load read a valid header */
    struct save_file_layout loaded_layout;
} state;

/* "save_file: <call>(<path>): <error>" on stderr. */
static void report(const char *call, const char *path, int error)
{
    (void)fprintf(stderr, "save_file: %s(%s): %s\n", call, path,
                  strerror(error)); /* best-effort: this is the error report */
}

static uint32_t get_u32le(const uint8_t bytes[SAVE_FILE_U32_BYTES])
{
    uint32_t value = 0;
    for (unsigned i = 0; i < SAVE_FILE_U32_BYTES; i++)
        value |= (uint32_t)bytes[i] << (CHAR_BIT * i);
    return value;
}

static void put_u32le(uint8_t bytes[SAVE_FILE_U32_BYTES], uint32_t value)
{
    for (unsigned i = 0; i < SAVE_FILE_U32_BYTES; i++)
        bytes[i] = (uint8_t)(value >> (CHAR_BIT * i));
}

uint32_t save_file_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = CRC32_INITIAL;
    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < CHAR_BIT; bit++)
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ CRC32_REFLECTED_POLYNOMIAL : crc >> 1;
    }
    return crc ^ CRC32_FINAL_XOR;
}

struct save_file_layout save_file_native_layout(void)
{
    return (struct save_file_layout){
        .pointer_bits = (uint32_t)(sizeof(void *) * CHAR_BIT),
        .layout_id = SAVE_FILE_LAYOUT_ID,
    };
}

static bool is_native(struct save_file_layout layout)
{
    const struct save_file_layout native = save_file_native_layout();
    return layout.pointer_bits == native.pointer_bits && layout.layout_id == native.layout_id;
}

/* Fills state's paths for path; false if it is unusable. */
static bool set_paths(const char *path)
{
    const size_t length = strlen(path);
    if (length == 0 || path[length - 1] == '/')
        return false; /* names no file */
    if (length + sizeof SAVE_FILE_TMP_SUFFIX > SAVE_FILE_PATH_SIZE)
        return false; /* no room for path + ".tmp" + terminator */
    memcpy(state.path, path, length + 1);
    memcpy(state.tmp_path, path, length);
    memcpy(state.tmp_path + length, SAVE_FILE_TMP_SUFFIX, sizeof SAVE_FILE_TMP_SUFFIX);
    return true;
}

int save_file_init(const char *path)
{
    state.initialised = false;
    state.has_loaded_layout = false;
    if (path == NULL) {
        state.persistent = false;
        state.initialised = true;
        return SAVE_FILE_OK;
    }
    if (!set_paths(path)) {
        (void)fprintf(stderr,
                      "save_file: save_file_init(%s): a save path must be a file name, "
                      "not empty, not ending in '/', at most %zu bytes\n",
                      path, (size_t)(SAVE_FILE_PATH_SIZE - sizeof SAVE_FILE_TMP_SUFFIX));
        return SAVE_FILE_ERR_PATH;
    }
    state.persistent = true;
    state.initialised = true;
    return SAVE_FILE_OK;
}

/* Reads exactly count bytes; false, with the reason printed, otherwise. */
static bool read_exactly(FILE *file, void *dest, size_t count)
{
    const size_t read = fread(dest, 1, count, file);
    if (read == count)
        return true;
    if (ferror(file))
        report("fread", state.path, errno);
    else
        (void)fprintf(stderr, "save_file: fread(%s): the file ended %zu bytes early\n", state.path,
                      count - read); /* best-effort */
    return false;
}

/* SAVE_FILE_CORRUPT, after printing why the file at state.path is unusable. */
PLATFORM_PRINTF_FORMAT(1, 2) static int corrupt(const char *format, ...)
{
    (void)fprintf(stderr, "save_file: %s: ", state.path); /* best-effort */
    va_list args;
    va_start(args, format);
    (void)vfprintf(stderr, format, args); /* best-effort */
    va_end(args);
    (void)fputs("; not loaded\n", stderr); /* best-effort */
    return SAVE_FILE_CORRUPT;
}

/* A file that starts with the magic; header holds its first
 * header_length bytes and the file is positioned after them. */
static int read_versioned(FILE *file, intmax_t file_size, const struct header_bytes *header, size_t header_length)
{
    if (header_length < SAVE_FILE_HEADER_SIZE)
        return corrupt("the header is cut short at %zu of %u bytes", header_length, SAVE_FILE_HEADER_SIZE);
    const uint32_t version = get_u32le(header->version);
    const struct save_file_layout layout = {
        .pointer_bits = get_u32le(header->pointer_bits),
        .layout_id = get_u32le(header->layout_id),
    };
    const uint32_t image_size = get_u32le(header->image_size);
    if (version != SAVE_FILE_FORMAT_VERSION)
        return corrupt("format version %" PRIu32 ", but this build reads version %u", version,
                       SAVE_FILE_FORMAT_VERSION);
    if (image_size != FLASH_CHIP_SIZE)
        return corrupt("the header gives an image of %" PRIu32 " bytes, but the 1 Mbit chip is "
                       "%zu bytes",
                       image_size, (size_t)FLASH_CHIP_SIZE);
    if (file_size != (intmax_t)SAVE_FILE_HEADER_SIZE + (intmax_t)FLASH_CHIP_SIZE)
        return corrupt("the file is %jd bytes, but a version %u save is %zu", file_size, SAVE_FILE_FORMAT_VERSION,
                       SAVE_FILE_HEADER_SIZE + FLASH_CHIP_SIZE);
    if (layout.pointer_bits != POINTER_BITS_32 && layout.pointer_bits != POINTER_BITS_64)
        return corrupt("the header gives a pointer width of %" PRIu32 " bits, not 32 or 64", layout.pointer_bits);
    if (!read_exactly(file, flash_image(), FLASH_CHIP_SIZE))
        return SAVE_FILE_CORRUPT;
    const uint32_t stored_crc = get_u32le(header->image_crc32);
    const uint32_t image_crc = save_file_crc32(flash_image(), FLASH_CHIP_SIZE);
    if (image_crc != stored_crc)
        return corrupt("the image's CRC-32 is 0x%08" PRIX32 ", but the header gives 0x%08" PRIX32, image_crc,
                       stored_crc);

    state.loaded_layout = layout;
    state.has_loaded_layout = true;
    if (is_native(layout))
        return SAVE_FILE_OK;
    const struct save_file_layout native = save_file_native_layout();
    (void)fprintf(stderr,
                  "save_file: %s was written by a %" PRIu32 "-bit build with layout %" PRIu32 "; this is a %" PRIu32
                  "-bit build with layout %" PRIu32 ", so it is loaded but foreign\n",
                  state.path, layout.pointer_bits, layout.layout_id, native.pointer_bits,
                  native.layout_id); /* best-effort */
    return SAVE_FILE_FOREIGN_LAYOUT;
}

/* A file without the magic, positioned at its start: a bare chip image. */
static int read_legacy(FILE *file, intmax_t file_size)
{
    if (file_size == FLASH_512K_CHIP_SIZE)
        return corrupt("%u bytes is a save from a 512 Kbit flash cartridge, but Emerald's chip "
                       "is 1 Mbit (%zu bytes)",
                       FLASH_512K_CHIP_SIZE, FLASH_CHIP_SIZE);
    if (file_size != (intmax_t)FLASH_CHIP_SIZE)
        return corrupt("%jd bytes, with no header, but a bare 1 Mbit flash image is %zu bytes", file_size,
                       FLASH_CHIP_SIZE);
    if (!read_exactly(file, flash_image(), FLASH_CHIP_SIZE))
        return SAVE_FILE_CORRUPT;
    return SAVE_FILE_LEGACY;
}

/* Reads the open save file into the image: SAVE_FILE_OK, SAVE_FILE_LEGACY,
 * SAVE_FILE_FOREIGN_LAYOUT, or SAVE_FILE_CORRUPT with the reason printed
 * (the image may then hold part of the file). */
static int read_save(FILE *file)
{
    struct stat info;
    if (fstat(fileno(file), &info) != 0) {
        report("fstat", state.path, errno);
        return SAVE_FILE_CORRUPT;
    }
    if (!S_ISREG(info.st_mode))
        return corrupt("not a regular file");
    const intmax_t file_size = (intmax_t)info.st_size;

    struct header_bytes header = {0}; /* only header_length bytes are read */
    const size_t header_length = file_size < (intmax_t)sizeof header ? (size_t)file_size : sizeof header;
    if (!read_exactly(file, &header, header_length))
        return SAVE_FILE_CORRUPT;
    if (header_length >= MAGIC_SIZE && memcmp(header.magic, magic, MAGIC_SIZE) == 0)
        return read_versioned(file, file_size, &header, header_length);
    if (fseek(file, 0, SEEK_SET) != 0) {
        report("fseek", state.path, errno);
        return SAVE_FILE_CORRUPT;
    }
    return read_legacy(file, file_size);
}

int save_file_load(void)
{
    if (!state.initialised) {
        (void)fputs("save_file: save_file_load: called before save_file_init\n", stderr); /* best-effort */
        return SAVE_FILE_ERR_NOT_INITIALISED;
    }
    state.has_loaded_layout = false;
    if (!state.persistent) {
        flash_erase_image();
        return SAVE_FILE_NEW;
    }
    FILE *const file = fopen(state.path, "rb");
    if (file == NULL) {
        const int error = errno;
        flash_erase_image();
        if (error == ENOENT)
            return SAVE_FILE_NEW;
        report("fopen", state.path, error);
        return SAVE_FILE_CORRUPT;
    }
    int status = read_save(file);
    if (fclose(file) != 0 && status != SAVE_FILE_CORRUPT) {
        report("fclose", state.path, errno);
        status = SAVE_FILE_CORRUPT;
    }
    if (status == SAVE_FILE_CORRUPT) {
        state.has_loaded_layout = false;
        flash_erase_image();
    }
    return status;
}

bool save_file_loaded_layout(struct save_file_layout *out)
{
    if (!state.has_loaded_layout || out == NULL)
        return false;
    *out = state.loaded_layout;
    return true;
}

/* The header for the current image, with this build's layout tag. */
static void encode_header(struct header_bytes *header)
{
    const struct save_file_layout native = save_file_native_layout();
    memcpy(header->magic, magic, MAGIC_SIZE);
    put_u32le(header->version, SAVE_FILE_FORMAT_VERSION);
    put_u32le(header->pointer_bits, native.pointer_bits);
    put_u32le(header->layout_id, native.layout_id);
    put_u32le(header->image_size, FLASH_CHIP_SIZE);
    put_u32le(header->image_crc32, save_file_crc32(flash_image(), FLASH_CHIP_SIZE));
}

/* Closes (if open) and removes the tmp file a failed store created, reporting
 * any failure; the store has already failed and reported why. */
static void discard_tmp(FILE *tmp)
{
    if (tmp != NULL && fclose(tmp) != 0)
        report("fclose", state.tmp_path, errno);
    if (remove(state.tmp_path) != 0)
        report("remove", state.tmp_path, errno);
}

/* Writes the header and the image to the tmp file and makes it durable. */
static int write_tmp(void)
{
    struct header_bytes header = {0};
    encode_header(&header);
    FILE *const tmp = fopen(state.tmp_path, "wb");
    if (tmp == NULL) {
        report("fopen", state.tmp_path, errno);
        return SAVE_FILE_ERR_IO; /* nothing created */
    }
    const char *failed_call = NULL;
    if (fwrite(&header, 1, sizeof header, tmp) != sizeof header
        || fwrite(flash_image(), 1, FLASH_CHIP_SIZE, tmp) != FLASH_CHIP_SIZE)
        failed_call = "fwrite";
    else if (fflush(tmp) != 0)
        failed_call = "fflush";
    else if (host_file_sync(fileno(tmp)) != 0)
        failed_call = "fsync";
    if (failed_call != NULL) {
        report(failed_call, state.tmp_path, errno);
        discard_tmp(tmp);
        return SAVE_FILE_ERR_IO;
    }
    if (fclose(tmp) != 0) {
        report("fclose", state.tmp_path, errno);
        discard_tmp(NULL);
        return SAVE_FILE_ERR_IO;
    }
    return SAVE_FILE_OK;
}

int save_file_store(void)
{
    if (!state.initialised) {
        (void)fputs("save_file: save_file_store: called before save_file_init\n", stderr); /* best-effort */
        return SAVE_FILE_ERR_NOT_INITIALISED;
    }
    if (!state.persistent)
        return SAVE_FILE_OK;
    const int written = write_tmp();
    if (written != SAVE_FILE_OK)
        return written;
    if (host_file_replace(state.tmp_path, state.path) != 0) {
        (void)fprintf(stderr, "save_file: rename(%s, %s): %s\n", state.tmp_path, state.path,
                      strerror(errno)); /* best-effort: this is the error report */
        discard_tmp(NULL);
        return SAVE_FILE_ERR_IO;
    }
    /* Syncs the directory holding the save, so the replacement is durable. */
    if (host_file_sync_directory_of(state.path) != 0) {
        (void)fprintf(stderr, "save_file: fsync of the directory of %s: %s\n", state.path,
                      strerror(errno)); /* best-effort: this is the error report */
        return SAVE_FILE_ERR_IO;
    }
    return SAVE_FILE_OK;
}

void save_file_shutdown(void)
{
    state.initialised = false;
    state.persistent = false;
    state.has_loaded_layout = false;
}
