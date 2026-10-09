/*
 * libgcc_division_ops.c: the eight division operators as plain C, so that
 * the compiler, not this file, picks the libgcc routine each one calls
 * (libgcc_division.h names them for both ABIs). Compiled twice by the
 * Makefile, each time defining the table DIVISION_OPS_NAME.
 */
#include "libgcc_division.h"

#ifndef DIVISION_OPS_NAME
#define DIVISION_OPS_NAME game_division_ops
#endif

static uint32_t udiv32(uint32_t n, uint32_t d)
{
    return n / d;
}

static uint32_t umod32(uint32_t n, uint32_t d)
{
    return n % d;
}

static int32_t sdiv32(int32_t n, int32_t d)
{
    return n / d;
}

static int32_t smod32(int32_t n, int32_t d)
{
    return n % d;
}

static uint64_t udiv64(uint64_t n, uint64_t d)
{
    return n / d;
}

static uint64_t umod64(uint64_t n, uint64_t d)
{
    return n % d;
}

static int64_t sdiv64(int64_t n, int64_t d)
{
    return n / d;
}

static int64_t smod64(int64_t n, int64_t d)
{
    return n % d;
}

const struct division_ops DIVISION_OPS_NAME = {udiv32, umod32, sdiv32, smod32, udiv64, umod64, sdiv64, smod64};
