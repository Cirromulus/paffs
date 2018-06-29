/*
 * Copyright (c) 2016-2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2016-2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#pragma once

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

//FIXME Debug
#define PAFFS_ENABLE_FAILPOINTS

#ifdef PAFFS_ENABLE_FAILPOINTS
#include <functional>
#endif

namespace paffs
{
typedef uint32_t TraceMask;
extern TraceMask traceMask;
extern const char* traceDescription[];

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef PAFFS_ENABLE_FAILPOINTS
extern std::function<void(const char*, unsigned int, unsigned int)> failCallback;
void failpointFn(const char*, unsigned int, unsigned int);
#define FAILPOINT                                       \
do                                                      \
{                                                       \
    /*TODO: Somehow notify something of this failpoint*/\
    failpointFn(__FILENAME__, __LINE__, __COUNTER__);       \
}while(false)
#else
#define FAILPOINT
#endif
}

// clang-format off

#define PAFFS_DBG(mask, msg, ...)                                           \
    do                                                                      \
    {                                                                       \
        if (((mask) & traceMask) == (mask))                                 \
        {                                                                   \
            fprintf(stderr,                                                 \
                    "paffs %s: " msg "\n\t-line %" PRId16 ", file %s\n",    \
                    traceDescription[ffs(mask)],                            \
                    ##__VA_ARGS__,                                          \
                    __LINE__,                                               \
                    __FILENAME__);                                          \
            if ((mask)&PAFFS_TRACE_BUG)                                     \
            {                                                               \
                raise(SIGINT);                                              \
            }                                                               \
        }                                                                   \
    } while (0)

#define PAFFS_DBG_S(mask, msg, ...)                                         \
    do                                                                      \
    {                                                                       \
        if (((mask) & traceMask) == (mask))                                 \
        {                                                                   \
            fprintf(stderr, "%s: " msg "\n",                                \
                    traceDescription[ffs(mask)], ##__VA_ARGS__);            \
        }                                                                   \
    } while (0)

#define PAFFS_TRACE_ALWAYS      0x00000000
#define PAFFS_TRACE_INFO        0x00000001
#define PAFFS_TRACE_OS          0x00000002
#define PAFFS_TRACE_DEVICE      0x00000004
#define PAFFS_TRACE_JOUR_PERS   0x00000008
#define PAFFS_TRACE_BAD_BLOCKS  0x00000010
#define PAFFS_TRACE_ERASE       0x00000020
#define PAFFS_TRACE_GC          0x00000040
#define PAFFS_TRACE_WRITE       0x00000080
#define PAFFS_TRACE_TRACING     0x00000100
#define PAFFS_TRACE_DELETION    0x00000200
#define PAFFS_TRACE_BUFFERS     0x00000400
#define PAFFS_TRACE_NANDACCESS  0x00000800
#define PAFFS_TRACE_GC_DETAIL   0x00001000
#define PAFFS_TRACE_PAGESTATEM  0x00002000
#define PAFFS_TRACE_AREA        0x00004000
#define PAFFS_TRACE_PACACHE     0x00008000

#define PAFFS_TRACE_VERIFY_TC   0x00010000
#define PAFFS_TRACE_VERIFY_DEV  0x00020000
#define PAFFS_TRACE_VERIFY_AS   0x00040000
#define PAFFS_WRITE_VERIFY_AS   0x00080000
#define PAFFS_TRACE_VERIFY_ALL  0x00070000

#define PAFFS_TRACE_SUPERBLOCK  0x00100000
#define PAFFS_TRACE_TREECACHE   0x00200000
#define PAFFS_TRACE_TREE        0x00400000
#define PAFFS_TRACE_MOUNT       0x00800000
#define PAFFS_TRACE_ASCACHE     0x01000000
#define PAFFS_TRACE_VERBOSE     0x02000000
#define PAFFS_TRACE_READ        0x04000000
#define PAFFS_TRACE_JOURNAL     0x08000000

#define PAFFS_TRACE_ERROR       0x10000000
#define PAFFS_TRACE_BUG         0x20000000
#define PAFFS_TRACE_ALL         0xfff7ffff
#define PAFFS_TRACE_SOME        0x30800071

// clang-format on
