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

#include "paffs.hpp"
#include "device.hpp"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

namespace paffs
{
//Note: Order is different
const Param stdParam{totalBytesPerPage,
                     pagesPerBlock,
                     blocksTotal,
                     oobBytesPerPage,
                     jumpPadNo,
                     dataBytesPerPage,
                     dataPagesPerArea,
                     totalPagesPerArea,
                     areaSummarySizePacked,
                     blocksPerArea,
                     superChainElems,
                     areasNo,
};

TraceMask traceMask =
        //PAFFS_TRACE_VERBOSE |
        //PAFFS_TRACE_JOURNAL |
        // PAFFS_TRACE_READ |
        PAFFS_TRACE_INFO |
        // PAFFS_TRACE_AREA |
        PAFFS_TRACE_ERROR |
        PAFFS_TRACE_BUG |
        // PAFFS_TRACE_TREE |
        // PAFFS_TRACE_TREECACHE |
        // PAFFS_TRACE_ASCACHE |
        // PAFFS_TRACE_SCAN |
        // PAFFS_TRACE_WRITE |
        // PAFFS_TRACE_SUPERBLOCK |
        // PAFFS_TRACE_ALLOCATE |
        // PAFFS_TRACE_VERIFY_AS |
        PAFFS_TRACE_VERIFY_TC |
        // PAFFS_WRITE_VERIFY_AS | //This may collide with ECC
        // PAFFS_TRACE_GC |
        // PAFFS_TRACE_GC_DETAIL |
        0;

const char* traceDescription[] =
   {
       "INVALID",
       "INFO",
       "OS",
       "DEVICE",
       "SCAN",
       "BAD_BLOCKS",
       "ERASE",
       "GC",
       "WRITE",
       "TRACING",
       "DELETION",
       "BUFFERS",
       "NANDACCESS",
       "GC_DETAIL",
       "SCAN_DEBUG",
       "AREA",
       "PACACHE",
       "VERIFY_TC",
       "VERIFY_NAND",
       "VERIFY_AS",
       "WRITE_VERIFY_AS",
       "SUPERBLOCK",
       "TREECACHE",
       "TREE",
       "MOUNT",
       "ASCACHE",
       "VERBOSE",
       "READ",
       "JOURNAL",
       "ERROR",
       "BUG",
       "ALWAYS"
   };

const char* resultMsg[] = {
    "ok",
    "Biterror could be corrected by ECC",
    "Biterror could not be corrected by ECC",
    "Object not found",
    "Object already exists",
    "Object too big",
    "Input values malformed",
    "Operation not yet supported",
    "An internal Error occured",
    "Node is already root, no Parent",
    "No (usable) space left on device",
    "Not enough RAM for cache",
    "Operation not permitted",
    "Directory is not empty",
    "Flash needs retirement",
    "Device is not mounted",
    "Device is already mounted",
    "Object name is too big",
    "Tried writing on readOnly Device",
    "Unknown error",
    "You should not be seeing this..."
};

const char*
err_msg(Result pr)
{
    return resultMsg[static_cast<uint8_t>(pr)];
}

void
Paffs::printCacheSizes()
{
    PAFFS_DBG_S(PAFFS_TRACE_INFO, "-----------Devices: %" PRIu8 "-----------", maxNumberOfDevices);
    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "TreeNode size: %zu Byte, TreeCacheNode size: %zu Byte. Cachable Nodes: %" PRIu8 ".\n"
                "\tBranch order: %" PRIu16 ", Leaf order: %" PRIu16 "\n"
                "\tOverall TreeCache size: %zu Byte.",
                sizeof(TreeNode),
                sizeof(TreeCacheNode),
                treeNodeCacheSize,
                branchOrder,
                leafOrder,
                treeNodeCacheSize * sizeof(TreeCacheNode));

    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "Packed AreaSummary size: %zu Byte. Cacheable Summaries: %" PRIu8 ".\n"
                "\tOverall AreaSummary cache size: %zu Byte.",
                dataPagesPerArea / 4 + 2 + sizeof(PageOffs),
                areaSummaryCacheSize,
                (dataPagesPerArea / 4 + 2 + sizeof(PageOffs)) * areaSummaryCacheSize);

    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "Size of AreaMap Entry: %zu Byte. Areas: %" PTYPE_AREAPOS ".\n"
                "\tOverall AreaMap Size: %zu Byte.",
                sizeof(Area),
                areasNo,
                sizeof(Area) * areasNo);

    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "Size of File object: %zu Byte. Max Open Files: %" PRIu8 ".\n"
                "\tOverall File cache Size: %zu Byte.",
                sizeof(Obj),
                maxNumberOfFiles,
                sizeof(Obj) * maxNumberOfFiles);

    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "Size of Inode object: %zu Byte. Max open Inodes: %" PRIu8 ".\n"
                "\tOverall Inode cache Size: %zu Byte.",
                sizeof(Inode),
                maxNumberOfInodes,
                sizeof(Inode) * maxNumberOfInodes);

    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "Size of Address: %zu. Addresses per Page: %" PRIu16 ". AddrListCacheElem: %zu Byte.\n"
                "\tOverall AddrBuffer Size: %zu Byte",
                sizeof(Addr),
                addrsPerPage,
                sizeof(AddrListCacheElem),
                sizeof(PageAddressCache));

    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "Size of Device: %zu. Number of Devices: %" PRIu8 ".\n"
                "\tOverall Devices Size: %zu Byte",
                sizeof(Device),
                maxNumberOfDevices,
                sizeof(Device) * maxNumberOfDevices);

    if(areaSummaryIsPacked)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ALWAYS, "\nWARNING: Using packed AreaSummaries (slower)");
    }

    PAFFS_DBG_S(PAFFS_TRACE_INFO, "--------------------------------\n");
}

Paffs::Paffs(std::vector<Driver*>& deviceDrivers)
{
    memset(validDevices, false, maxNumberOfDevices * sizeof(bool));
    memset(devices, 0, maxNumberOfDevices * sizeof(void*));
    int i = 0;
    for (auto it = deviceDrivers.begin(); it != deviceDrivers.end();
         ++it, i++)
    {
        if (i >= maxNumberOfDevices)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Too many device Drivers given! Accepting max %" PRId16 ".",
                      maxNumberOfDevices);
            break;
        }
        validDevices[i] = true;
        devices[i] = new (deviceMemory[i]) Device(**it);
    }
    printCacheSizes();
}
Paffs::~Paffs()
{
    for (int i = 0; i < maxNumberOfDevices; i++)
    {
        if (devices[i] != nullptr)
        {
            Driver* drv = &devices[i]->driver;
            devices[i]->~Device();
            delete drv;
        }
    }
};

Result
Paffs::getLastErr()
{
    // TODO: Actually choose which error to display
    return devices[0]->lasterr;
}

void
Paffs::resetLastErr()
{
    for (uint8_t i = 0; i < maxNumberOfDevices; i++)
    {
        if (validDevices[i])
            devices[i]->lasterr = Result::ok;
    }
}

Result
Paffs::format(const BadBlockList badBlockList[maxNumberOfDevices], bool complete)
{
    // TODO: Handle errors
    PAFFS_DBG_S(PAFFS_TRACE_INFO, "--------------------");

    PAFFS_DBG_S(PAFFS_TRACE_INFO,
                "Formatting infos:\n\t"
                "dataBytesPerPage  : %04u\n\t"
                "oobBytesPerPage   : %04u\n\t"
                "pagesPerBlock     : %04u\n\t"
                "blocks            : %04u\n\t"
                "blocksPerArea     : %04u\n\t"
                "jumpPadNo         : %04u\n\n\t"

                "totalBytesPerPage : %04u\n\t"
                "areasNo           : %04u\n\t"
                "totalPagesPerArea : %04u\n\t"
                "dataPagesPerArea  : %04u\n\t"
                "areaSummarySize   : %04u\n\t"
                "superChainElems   : %04u\n\t",
                dataBytesPerPage,
                oobBytesPerPage,
                pagesPerBlock,
                blocksTotal,
                blocksPerArea,
                jumpPadNo,
                totalBytesPerPage,
                areasNo,
                totalPagesPerArea,
                dataPagesPerArea,
                areaSummarySizePacked,
                superChainElems);

    PAFFS_DBG_S(PAFFS_TRACE_INFO, "--------------------\n");

    if(traceMask & PAFFS_TRACE_INFO)
    {
        printf("Tracing ");
        for(uint8_t i = 0; i < sizeof(TraceMask) * 8; i++)
        {
            if(1 << i & traceMask)
            {
                printf("%s ", traceDescription[i + 1]);
            }
        }
        printf("\n");
    }

    Result globalReturn = Result::ok;
    for (uint8_t i = 0; i < maxNumberOfDevices; i++)
    {
        if (validDevices[i])
        {
            Result deviceReturn = devices[i]->format(badBlockList[i], complete);
            if (deviceReturn > globalReturn)
            {   //Results are (somewhat) ordered by badness
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not format device %" PRId16 "!", i);
                globalReturn = deviceReturn;
            }
        }
    }
    return globalReturn;
}

Result
Paffs::mount(bool readOnly)
{
    // TODO: Handle errors
    Result globalReturn = Result::ok;
    for (uint8_t i = 0; i < maxNumberOfDevices; i++)
    {
        if (validDevices[i])
        {
            Result deviceReturn = devices[i]->mnt(readOnly);
            if (deviceReturn > globalReturn)
            {   //Results are (somewhat) ordered by badness
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not mount device %" PRId16 "!", i);
                globalReturn = deviceReturn;
            }
        }
    }
    return globalReturn;
}
Result
Paffs::unmount()
{
    // TODO: Handle errors
    Result globalReturn = Result::ok;
    for (uint8_t i = 0; i < maxNumberOfDevices; i++)
    {
        if (validDevices[i])
        {
            Result deviceReturn = devices[i]->unmnt();
            if (deviceReturn > globalReturn)
            {   //Results are (somewhat) ordered by badness
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not unmount device %" PRId16 "!", i);
                globalReturn = deviceReturn;
            }
        }
    }
    return globalReturn;
}

Result
Paffs::mkDir(const char* fullPath, Permission mask)
{
    // TODO: Handle errors
    Result globalReturn = Result::ok;
    for (uint8_t i = 0; i < maxNumberOfDevices; i++)
    {
        if (validDevices[i])
        {
            Result deviceReturn = devices[i]->mkDir(fullPath, mask);
            if (deviceReturn > globalReturn)
            {   //Results are (somewhat) ordered by badness
                globalReturn = deviceReturn;
            }
        }
    }
    return globalReturn;
}

Dir*
Paffs::openDir(const char* path)
{
    // TODO: Handle multiple positions
    return devices[0]->openDir(path);
}

Result
Paffs::closeDir(Dir*& dir)
{
    // TODO: Handle multiple positions
    return devices[0]->closeDir(dir);
}

Dirent*
Paffs::readDir(Dir& dir)
{
    // TODO: Handle multiple positions
    return devices[0]->readDir(dir);
}

void
Paffs::rewindDir(Dir& dir)
{
    // TODO: Handle multiple positions
    devices[0]->rewindDir(dir);
}

Obj*
Paffs::open(const char* path, Fileopenmask mask)
{
    return devices[0]->open(path, mask);
}

Result
Paffs::close(Obj& obj)
{
    return devices[0]->close(obj);
}

Result
Paffs::touch(const char* path)
{
    return devices[0]->touch(path);
}

Result
Paffs::getObjInfo(const char* fullPath, ObjInfo& nfo)
{
    return devices[0]->getObjInfo(fullPath, nfo);
}

Result
Paffs::read(Obj& obj, void* buf, FileSize bytesToRead, FileSize* bytesRead)
{
    return devices[0]->read(obj, buf, bytesToRead, bytesRead);
}

Result
Paffs::write(Obj& obj, const void* buf, FileSize bytesToWrite, FileSize* bytesWritten)
{
    return devices[0]->write(obj, buf, bytesToWrite, bytesWritten);
}

Result
Paffs::seek(Obj& obj, FileSizeDiff m, Seekmode mode)
{
    return devices[0]->seek(obj, m, mode);
}

Result
Paffs::flush(Obj& obj)
{
    return devices[0]->flush(obj);
}

Result
Paffs::truncate(const char* path, FileSize newLength)
{
    return devices[0]->truncate(path, newLength);
}

Result
Paffs::chmod(const char* path, Permission perm)
{
    return devices[0]->chmod(path, perm);
}
Result
Paffs::remove(const char* path)
{
    return devices[0]->remove(path);
}

Result
Paffs::getListOfOpenFiles(Obj* list[])
{
    return devices[0]->getListOfOpenFiles(list);
}

uint8_t
Paffs::getNumberOfOpenFiles()
{
    return devices[0]->getNumberOfOpenFiles();
}
uint8_t
Paffs::getNumberOfOpenInodes()
{
    return devices[0]->getNumberOfOpenInodes();
}

// ONLY FOR DEBUG
Device*
Paffs::getDevice(uint16_t number)
{
    if (number >= maxNumberOfDevices)
    {
        return nullptr;
    }
    return devices[number];
}

void
Paffs::setTraceMask(TraceMask mask)
{
    traceMask = mask;
}

TraceMask
Paffs::getTraceMask()
{
    return traceMask;
}
}
