/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include "paffs_trace.hpp"
#include "smartInodePtr.hpp"
#include <outpost/time/time_epoch.h>
#include <stdint.h>
#include <inttypes.h>

#pragma once

namespace paffs
{
static const uint8_t version = 1;

// TODO: Elaborate certain order of badness
enum class Result : uint8_t
{
    ok = 0,
    biterrorCorrected,
    biterrorNotCorrected,
    notFound,
    exists,
    toobig,
    invalidInput,
    nimpl,
    bug,
    noparent,
    nospace,
    lowmem,
    noperm,
    dirnotempty,
    badflash,
    notMounted,
    alrMounted,
    objNameTooLong,
    readonly,
    fail,
    num_result
};

extern const char* areaNames[];          // Initialized in area.cpp
extern const char* areaStatusNames[];    // Initialized in area.cpp
extern const char* summaryEntryNames[];  // Initialized in area.cpp
extern const char* resultMsg[];          // Initialized in paffs.cpp

typedef uint8_t Permission;

static constexpr Permission R = 0x1;
static constexpr Permission W = 0x2;
static constexpr Permission X = 0x4;
static constexpr Permission permMask = R | W | X;

typedef uint8_t Fileopenmask;
static constexpr Fileopenmask FR = 0x01;   // file read
static constexpr Fileopenmask FW = 0x02;   // file write
static constexpr Fileopenmask FEX= 0x04;   // file execute
static constexpr Fileopenmask FA = 0x08;   // file append
static constexpr Fileopenmask FE = 0x10;   // file open only existing
static constexpr Fileopenmask FC = 0x20;   // file create

enum class Seekmode
{
    set = 0,
    cur,
    end
};

struct Param
{
    //Note: Order of config mixed for better alignment of members
    uint16_t totalBytesPerPage;
    uint16_t pagesPerBlock;
    uint16_t blocksTotal;
    uint8_t  oobBytesPerPage;
    uint8_t  jumpPadNo;
    // Automatically filled//
    uint16_t dataBytesPerPage;
    uint16_t dataPagesPerArea;
    uint16_t totalPagesPerArea;
    uint16_t areaSummarySize;
    uint8_t  blocksPerArea;
    uint8_t  superChainElems;
    uint32_t areasNo;
};

extern const Param stdParam;

typedef uint32_t Addr;           // Contains logical area and relative page
#define PTYPE_ADDR PRIu32
typedef uint16_t AreaPos;        // Has to address total areas
#define PTYPE_AREAPOS PRIu16
typedef uint16_t PageOffs;       // Has to address pages per area
#define PTYPE_PAGEOFFS PRIu16
typedef Addr     PageAbs;        // has to address all pages in a device
#define PTYPE_PAGEABS PTYPE_ADDR
typedef uint16_t BlockAbs;       // has to address all blocks in a device
#define PTYPE_BLOCKABS PRIu16
typedef uint32_t FileSize;       //~ 4 GB per file
#define PTYPE_FILSIZE PRIu32
typedef int64_t  FileSizeDiff;
#define PTYPE_FILSIZEDIFF PRId64
typedef uint16_t FileNamePos;
#define PTYPE_FILNAMEPOS PRIu16
typedef uint32_t InodeNo;        //~ 4 Million files
#define PTYPE_INODENO PRIu32
typedef uint16_t DirEntryCount;  // 65,535 Entries per Directory
#define PTYPE_DIRENTRYCOUNT PRIu16
typedef uint8_t DirEntryLength;  // 255 characters per Directory entry
#define PTYPE_DIRENTRYLEN PRIu8
static constexpr DirEntryLength maxDirEntryLength = 255;

struct BadBlockList
{
    BlockAbs* mList;
    uint16_t mSize;
    inline
    BadBlockList() : mList(nullptr), mSize(0){};
    inline
    BadBlockList(const BlockAbs* list, const uint16_t size) : mSize(size)
    {
        mList = new BlockAbs[size];
        memcpy(mList, list, size * sizeof(BlockAbs));
    };
    inline
    BadBlockList(BadBlockList const& other)
    {
        *this = other;
    }
    inline BadBlockList&
    operator=(BadBlockList const& other)
    {
        BlockAbs* tmpList = new BlockAbs[other.mSize];
        memcpy(tmpList, other.mList, other.mSize * sizeof(BlockAbs));
        mSize = other.mSize;
        mList = tmpList;
        return *this;
    }
    inline
    ~BadBlockList()
    {
        if (mList != nullptr)
        {
            delete[] mList;
        }
    }
    inline BlockAbs
    operator[](size_t pos) const
    {
        if (pos > mSize)
        {
            return 0;
        }
        return mList[pos];
    }
};

enum class InodeType : uint8_t
{
    file,
    dir,
    lnk
};

static constexpr uint16_t directAddrCount = 11;

struct Inode
{
    InodeNo no;
    InodeType type;  //2 Bit;
    Permission perm : 3;
    uint32_t reservedPages;  // Space on filesystem used in Pages
    FileSize size;           // Space on filesystem needed in bytes
    uint64_t crea;
    uint64_t mod;
    Addr direct[directAddrCount];
    Addr indir;
    Addr d_indir;
    Addr t_indir;
    // TODO: Add pointer to a PAC if cached
};

// could be later used for caching file paths, can be a file, directory or link
struct Dirent
{
    InodeNo no;          // This is used for lazy loading of Inode
    SmartInodePtr node;  // can be NULL if not loaded yet
    Dirent* parent;
    char* name;
};

// An object is a file
struct Obj
{
    bool rdnly;
    Dirent dirent;
    FileSize fp;  // Current filepointer
    Fileopenmask fo;  // TODO actually use this for read/write
};

struct Dir
{
    Dirent* self;
    Dirent* childs;
    DirEntryCount entries;
    DirEntryCount pos;
};

struct ObjInfo
{
    FileSize size;
    outpost::time::GpsTime created;
    outpost::time::GpsTime modified;
    bool isDir;
    Permission perm;
};

enum AreaType : uint8_t
{
    unset = 0,
    superblock,
    journal,
    index,
    data,
    garbageBuffer,
    retired,
    no
};

enum AreaStatus : uint8_t
{
    closed = 0,
    active,
    empty
};

enum class SummaryEntry : uint8_t
{
    free = 0,
    used,  // if read from super index, used can mean both free and used to save a bit per entry.
    dirty,
    error
};

struct Area
{  // TODO: Maybe packed? Slow, but less RAM
    // AreaType type:4;
    // AreaStatus status:2;
    AreaType type;
    AreaStatus status;
    uint32_t erasecount : 17;  // Overflow at 132.000 is acceptable (assuming less than 100k erase
                               // cycles)
    AreaPos position;  // physical position, not logical
};                     // 4 + 2 + 17 + 32 = 55 Bit = 7 Byte (8 on RAM)

const char*
err_msg(Result pr);  // implemented in paffs.cpp

template <typename T, size_t N>
constexpr size_t
size(T (&)[N])
{
    return N;
}

class Device;
class Driver;

}  // namespace paffs

// clang-format off
#include <paffs/config.hpp>
#include "config/auto.hpp"
// clang-format on
