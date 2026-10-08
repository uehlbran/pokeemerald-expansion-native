/* save_file.c: empty module; the tests in test/test_save_file.c come first. */
#include "save_file.h"

int save_file_init(const char *path)
{
    (void)path;
    return SAVE_FILE_ERR_PATH;
}

int save_file_load(void)
{
    return SAVE_FILE_ERR_NOT_INITIALISED;
}

int save_file_store(void)
{
    return SAVE_FILE_ERR_NOT_INITIALISED;
}

void save_file_shutdown(void)
{
}
