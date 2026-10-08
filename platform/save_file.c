/*
 * save_file.c: the cartridge's persistence, the flash image kept in a .sav
 * file. Interface, file format and how main uses it: save_file.h.
 *
 * Stands in for the flash chip's non-volatility (GBATEK "GBA Cart Backup
 * Flash ROM", https://problemkaputt.de/gbatek.htm#gbacartbackupflashrom:
 * the save survives power-off). Written from POSIX and docs/RESEARCH.md,
 * "Save"; the ports' save code was read for its pitfalls only
 * (docs/research/port-*.md): reads from a second file handle, unflushed
 * writes, no atomic replacement, and regionalidades' rename without fsync.
 *
 * Load: fopen, fstat for the type and size, one fread of the whole chip
 * straight into the image; anything short of a regular file of exactly
 * FLASH_CHIP_SIZE bytes read in full is reported and leaves an erased chip.
 * Store: the image goes to <path>.tmp, which is flushed, fsynced and closed
 * before rename() replaces <path> with it (atomic on POSIX), and then the
 * directory is fsynced so the rename itself survives a crash. On any failure
 * before the rename, the tmp file this store created is removed and <path>
 * is untouched.
 *
 * Module state: the paths, fixed buffers of SAVE_FILE_PATH_SIZE, computed
 * once by save_file_init. Nothing is allocated.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "flash.h"
#include "save_file.h"

/* Appended to the save path for the file a store writes before renaming. */
#define SAVE_FILE_TMP_SUFFIX ".tmp"

/* GBATEK "GBA Cart Backup Flash ROM": "64 KBytes - 512Kbits Flash ROM", the
 * smaller chip; a .sav of this size is from a different cartridge type. */
#define FLASH_512K_CHIP_SIZE 0x10000u

/* The file is the chip image byte for byte. The game's footers are u16 and
 * u32 fields written by the CPU, little-endian on the ARM7TDMI; on a
 * little-endian host they have the same bytes, so files move between the
 * GBA, emulators and this build unchanged. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
               "a .sav holds little-endian fields; a big-endian host needs the game to swap them");

static struct {
    bool initialised;
    bool persistent; /* false: save_file_init(NULL), in memory only */
    char path[SAVE_FILE_PATH_SIZE];
    char tmp_path[SAVE_FILE_PATH_SIZE]; /* path + SAVE_FILE_TMP_SUFFIX */
    char dir_path[SAVE_FILE_PATH_SIZE]; /* the directory holding path, fsynced after a rename */
} state;

/* "save_file: <call>(<path>): <error>" on stderr. */
static void report(const char *call, const char *path, int error)
{
    (void)fprintf(stderr, "save_file: %s(%s): %s\n", call, path,
                  strerror(error)); /* best-effort: this is the error report */
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

    const char *const last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        memcpy(state.dir_path, ".", sizeof ".");
    } else {
        /* "/x" is in "/"; "a/b/x" is in "a/b". */
        const size_t dir_length = last_slash == path ? 1 : (size_t)(last_slash - path);
        memcpy(state.dir_path, path, dir_length);
        state.dir_path[dir_length] = '\0';
    }
    return true;
}

int save_file_init(const char *path)
{
    state.initialised = false;
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

/* Reads the open save file into the image if it is a regular file of
 * exactly the chip's size: SAVE_FILE_OK, or SAVE_FILE_CORRUPT with the
 * reason printed (the image may then hold part of the file). */
static int read_image(FILE *file)
{
    struct stat info;
    if (fstat(fileno(file), &info) != 0) {
        report("fstat", state.path, errno);
        return SAVE_FILE_CORRUPT;
    }
    if (!S_ISREG(info.st_mode)) {
        (void)fprintf(stderr, "save_file: %s is not a regular file; not loaded\n",
                      state.path); /* best-effort */
        return SAVE_FILE_CORRUPT;
    }
    if (info.st_size == (off_t)FLASH_512K_CHIP_SIZE) {
        (void)fprintf(stderr,
                      "save_file: %s is %u bytes, a save from a 512 Kbit flash cartridge; "
                      "Emerald's chip is 1 Mbit (%u bytes), so it is not loaded\n",
                      state.path, FLASH_512K_CHIP_SIZE, FLASH_CHIP_SIZE); /* best-effort */
        return SAVE_FILE_CORRUPT;
    }
    if (info.st_size != (off_t)FLASH_CHIP_SIZE) {
        (void)fprintf(stderr,
                      "save_file: %s is %jd bytes, but a 1 Mbit flash save is %u bytes; "
                      "not loaded\n",
                      state.path, (intmax_t)info.st_size, FLASH_CHIP_SIZE); /* best-effort */
        return SAVE_FILE_CORRUPT;
    }
    const size_t read = fread(flash_image(), 1, FLASH_CHIP_SIZE, file);
    if (read != FLASH_CHIP_SIZE) {
        if (ferror(file))
            report("fread", state.path, errno);
        else
            (void)fprintf(stderr, "save_file: fread(%s): the file ended after %zu bytes\n",
                          state.path, read); /* best-effort */
        return SAVE_FILE_CORRUPT;
    }
    return SAVE_FILE_OK;
}

int save_file_load(void)
{
    if (!state.initialised) {
        (void)fputs("save_file: save_file_load: called before save_file_init\n",
                    stderr); /* best-effort */
        return SAVE_FILE_ERR_NOT_INITIALISED;
    }
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
    int status = read_image(file);
    if (fclose(file) != 0 && status == SAVE_FILE_OK) {
        report("fclose", state.path, errno);
        status = SAVE_FILE_CORRUPT;
    }
    if (status != SAVE_FILE_OK)
        flash_erase_image();
    return status;
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

/* Writes the image to the tmp file and makes it durable. */
static int write_tmp(void)
{
    FILE *const tmp = fopen(state.tmp_path, "wb");
    if (tmp == NULL) {
        report("fopen", state.tmp_path, errno);
        return SAVE_FILE_ERR_IO; /* nothing created */
    }
    const char *failed_call = NULL;
    if (fwrite(flash_image(), 1, FLASH_CHIP_SIZE, tmp) != FLASH_CHIP_SIZE)
        failed_call = "fwrite";
    else if (fflush(tmp) != 0)
        failed_call = "fflush";
    else if (fsync(fileno(tmp)) != 0)
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

/* fsyncs the directory holding the save, so the rename is durable. */
static int sync_directory(void)
{
    const int dir = open(state.dir_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) {
        report("open", state.dir_path, errno);
        return SAVE_FILE_ERR_IO;
    }
    int status = SAVE_FILE_OK;
    if (fsync(dir) != 0) {
        report("fsync", state.dir_path, errno);
        status = SAVE_FILE_ERR_IO;
    }
    if (close(dir) != 0) {
        report("close", state.dir_path, errno);
        status = SAVE_FILE_ERR_IO;
    }
    return status;
}

int save_file_store(void)
{
    if (!state.initialised) {
        (void)fputs("save_file: save_file_store: called before save_file_init\n",
                    stderr); /* best-effort */
        return SAVE_FILE_ERR_NOT_INITIALISED;
    }
    if (!state.persistent)
        return SAVE_FILE_OK;
    const int written = write_tmp();
    if (written != SAVE_FILE_OK)
        return written;
    if (rename(state.tmp_path, state.path) != 0) {
        (void)fprintf(stderr, "save_file: rename(%s, %s): %s\n", state.tmp_path, state.path,
                      strerror(errno)); /* best-effort: this is the error report */
        discard_tmp(NULL);
        return SAVE_FILE_ERR_IO;
    }
    return sync_directory();
}

void save_file_shutdown(void)
{
    state.initialised = false;
    state.persistent = false;
}

/* Not yet implemented: the versioned format's tests come first. */
bool save_file_loaded_layout(struct save_file_layout *out)
{
    (void)out;
    return false;
}

struct save_file_layout save_file_native_layout(void)
{
    return (struct save_file_layout){ 0, 0 };
}

uint32_t save_file_crc32(const uint8_t *bytes, size_t length)
{
    (void)bytes;
    (void)length;
    return 0;
}
