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

#include "device.hpp"
#include "driver/driver.hpp"
#include "paffs_trace.hpp"
#include <inttypes.h>

namespace paffs
{
outpost::rtos::SystemClock systemClock;

Device::Device(Driver& _driver)
    : driver(_driver),
      lasterr(Result::ok),
      mounted(false),
      readOnly(false),
      tree(this),
      sumCache(this),
      areaMgmt(this),
      dataIO(this),
      superblock(this),
      journalPersistence(this),
      journal(journalPersistence, superblock, sumCache, tree){};

Device::~Device()
{
    if (mounted)
    {
        fprintf(stderr,
                "Destroyed Device-Object without unmouning! "
                "This will most likely destroy "
                "the filesystem on flash.\n");
        Result r = destroyDevice();
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not (gracefully) destroy Device!");
        }
    }
}

Result
Device::format(const BadBlockList& badBlockList, bool complete)
{
    if (mounted)
        return Result::alrMounted;

    Result r = initializeDevice();
    if (r != Result::ok)
        return r;
    r = driver.initializeNand();
    if (r != Result::ok)
        return r;

    if (complete)
        PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Deleting all areas.\n");

    unsigned char hadAreaType = 0;
    unsigned char hadSuperblocks = 0;

    for (unsigned int block = 0; block < badBlockList.mSize; block++)
    {
        AreaPos area = badBlockList[block] / blocksPerArea;
        PAFFS_DBG_S(
                PAFFS_TRACE_BAD_BLOCKS, "Retiring Area %" PRIu32 " because of given List", area);

        if (badBlockList[block] > blocksTotal)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Invalid Bad Block given! "
                      "was %" PRIu32 " area %" PRIu32 ", should < %" PRIu32,
                      badBlockList[block],
                      area,
                      blocksTotal);
            return Result::einval;
        }
        if (area == 0)
        {
            // First and reserved Area
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Bad block in reserved first Area!");
            return Result::fail;
        }
        driver.markBad(badBlockList[block]);
        areaMgmt.setType(area, AreaType::retired);
    }

    for (unsigned int area = 0; area < areasNo; area++)
    {
        areaMgmt.setStatus(area, AreaStatus::empty);
        // erasecount is already set to 0
        areaMgmt.setPos(area, area);

        if (areaMgmt.getType(area) == AreaType::retired)
        {
            continue;
        }

        bool anyBlockInAreaBad = false;
        for (unsigned block = 0; block < blocksPerArea; block++)
        {
            if (driver.checkBad(area * blocksPerArea + block) != Result::ok)
            {
                PAFFS_DBG_S(PAFFS_TRACE_BAD_BLOCKS,
                            "Found marked bad block %" PRIu32 " during formatting, "
                            "retiring area %" PRIu32,
                            area * blocksPerArea + block,
                            area);
                anyBlockInAreaBad = true;
            }
        }
        if (anyBlockInAreaBad)
        {
            areaMgmt.setType(area, AreaType::retired);
            continue;
        }

        if (hadAreaType & (1 << AreaType::superblock |
                           // 1 << AreaType::journal |
                           1 << AreaType::garbageBuffer)
            || complete)
        {
            for (unsigned int p = 0; p < blocksPerArea; p++)
            {
                r = driver.eraseBlock(p + area * blocksPerArea);
                if (r != Result::ok)
                {
                    PAFFS_DBG_S(PAFFS_TRACE_BAD_BLOCKS,
                                "Found non-marked bad block %u during formatting, "
                                "retiring area %" PRIu32,
                                p + area * blocksPerArea,
                                area);
                    areaMgmt.setType(area, AreaType::retired);
                    break;
                }
            }
            areaMgmt.increaseErasecount(area);

            if (areaMgmt.getType(area) == AreaType::retired)
            {
                continue;
            }
        }

        if (!(hadAreaType & 1 << AreaType::superblock))
        {
            areaMgmt.initAreaAs(area, AreaType::superblock);
            areaMgmt.setActiveArea(AreaType::superblock, 0);
            if (++hadSuperblocks == superChainElems)
                hadAreaType |= 1 << AreaType::superblock;
            continue;
        }

        /*
        if(!(hadAreaType & 1 << AreaType::journal)){
            areaMgmt.initAreaAs(area, AreaType::journal);
            hadAreaType |= 1 << AreaType::journal;
            continue;
        }*/

        if (!(hadAreaType & 1 << AreaType::garbageBuffer))
        {
            areaMgmt.initAreaAs(area, AreaType::garbageBuffer);
            hadAreaType |= 1 << AreaType::garbageBuffer;
            continue;
        }

        areaMgmt.setType(area, AreaType::unset);
    }

    r = tree.start_new_tree();
    if (r != Result::ok)
        return r;
    {
        SmartInodePtr rootDir;
        r = createDirInode(rootDir, R | W | X);
        if (r != Result::ok)
        {
            destroyDevice();
            return r;
        }
        if (rootDir->no != 0)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Root Dir has Number != 0!");
            return Result::bug;
        }
        r = tree.insertInode(*rootDir);
        if (r != Result::ok)
        {
            destroyDevice();
            return r;
        }
    }
    r = tree.commitCache();
    if (r != Result::ok)
    {
        destroyDevice();
        return r;
    }
    r = sumCache.commitAreaSummaries(true);
    if (r != Result::ok)
    {
        destroyDevice();
        return r;
    }

    destroyDevice();
    driver.deInitializeNand();
    return Result::ok;
}

Result
Device::mnt(bool readOnlyMode)
{
    readOnly = readOnlyMode;

    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "mount with valid driver");

    if (mounted)
        return Result::alrMounted;

    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "not yet mounted");

    Result r = initializeDevice();
    if (r != Result::ok)
        return r;
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Inited Device");
    r = driver.initializeNand();
    if (r != Result::ok)
        return r;
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Inited Driver");

    r = sumCache.loadAreaSummaries();
    if (r == Result::nf)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "Tried mounting a device with an empty superblock!\n"
                    "Maybe not formatted?");
        destroyDevice();
        return r;
    }
    if (r != Result::ok)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not load Area Summaries");
        return r;
    }

    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Area Summaries loaded");

    r = journal.enable();
    if (r != Result::ok)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not enable Journal!");
        return r;
    }
    r = journal.processBuffer();
    if (r != Result::ok)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not process journal!");
        return r;
    }

    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Replayed Journal if needed");

    SmartInodePtr rootDir;
    r = findOrLoadInode(0, rootDir);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Could not load Inode of Root Dir!");
        return r;
    }
    // TODO: Supress decrease or increase reference to node 0 manually
    mounted = true;
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Mount sucessful");
    return r;
}
Result
Device::unmnt()
{
    if (!mounted)
        return Result::notMounted;
    Result r;

    InodePool<maxNumberOfInodes>::InodeMap::iterator it = inodePool.map.begin();
    if (it != inodePool.map.end())
    {
        PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Unclosed files remain, closing for unmount");
        while (it != inodePool.map.end())
        {
            PAFFS_DBG_S(PAFFS_TRACE_ALWAYS,
                        "Close Inode %" PRIu32 " with %u references",
                        it->first,
                        it->second.second);
            // TODO: Later, we would choose the actual pac instance (or the PAC will choose the
            // actual Inode...
            r = dataIO.pac.setTargetInode(*it->second.first);
            if (r != Result::ok)
            {
                // we ignore Result, because we unmount.
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load pac of an open file");
            }
            r = dataIO.pac.commit();
            if (r != Result::ok)
            {
                // we ignore Result, because we unmount.
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit pac of an open file");
            }

            inodePool.pool.freeObject(*it->second.first);
            it = inodePool.map.erase(it);
        }
    }

    r = tree.commitCache();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Tree cache!");
        return r;
    }

    r = sumCache.commitAreaSummaries();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Area Summaries!");
        return r;
    }

    if (traceMask & PAFFS_TRACE_AREA)
    {
        printf("Info: \n\t%" PRIu32 " used Areas\n", areaMgmt.getUsedAreas());
        for (unsigned int i = 0; i < areasNo; i++)
        {
            printf("\tArea %03d on %03u as %10s from page %4d %s\n",
                   i,
                   areaMgmt.getPos(i),
                   areaNames[areaMgmt.getType(i)],
                   areaMgmt.getPos(i) * blocksPerArea * pagesPerBlock,
                   areaStatusNames[areaMgmt.getStatus(i)]);
            if (i > 128)
            {
                printf("\n -- truncated 128-%u Areas.\n", areasNo);
                break;
            }
        }
        printf("\t----------------------\n");
    }

    journal.checkpoint();
    journal.clear();

    destroyDevice();
    driver.deInitializeNand();
    // just for cleanup & tests
    tree.wipeCache();
    mounted = false;
    return Result::ok;
}

Result
Device::createInode(SmartInodePtr& outInode, Permission mask)
{
    // FIXME: is this the best way to find a new number?
    InodeNo no;
    Result r = tree.findFirstFreeNo(&no);
    if (r != Result::ok)
        return r;
    r = inodePool.requireNewInode(no, outInode);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not require new Inode from Pool");
        return r;
    }
    outInode->perm = (mask & permMask);
    if (mask & W)
        outInode->perm |= R;
    outInode->size = 0;
    outInode->reservedPages = 0;
    outInode->crea =
            systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();
    outInode->mod = outInode->crea;
    return Result::ok;
}

/**
 * creates DirInode ONLY IN RAM
 */
Result
Device::createDirInode(SmartInodePtr& outInode, Permission mask)
{
    if (createInode(outInode, mask) != Result::ok)
        return Result::bug;
    outInode->type = InodeType::dir;
    outInode->size = 0;
    outInode->reservedPages = 0;
    return Result::ok;
}

/**
 * creates FilInode ONLY IN RAM
 */
Result
Device::createFilInode(SmartInodePtr& outInode, Permission mask)
{
    if (createInode(outInode, mask) != Result::ok)
    {
        return Result::bug;
    }
    outInode->type = InodeType::file;
    return Result::ok;
}

Result
Device::getParentDir(const char* fullPath, SmartInodePtr& parDir, unsigned int* lastSlash)
{
    if (fullPath[0] == 0)
    {
        lasterr = Result::einval;
        return Result::einval;
    }

    unsigned int p = 0;
    *lastSlash = 0;

    while (fullPath[p] != 0)
    {
        if (fullPath[p] == '/' && fullPath[p + 1] != 0)
        {  // Nicht /a/b/c/ erkennen, sondern /a/b/c
            *lastSlash = p + 1;
        }
        p++;
    }

    char* pathC = new char[*lastSlash + 1];
    memcpy(pathC, fullPath, *lastSlash);
    pathC[*lastSlash] = 0;

    Result r = getInodeOfElem(parDir, pathC);
    delete[] pathC;
    return r;
}

// Currently Linearer Aufwand
Result
Device::getInodeNoInDir(InodeNo& outInode, Inode& folder, const char* name)
{
    if (folder.type != InodeType::dir)
    {
        return Result::bug;
    }
    if (folder.size <= sizeof(DirEntryCount))
    {
        // Just contains a zero for "No entrys"
        return Result::nf;
    }

    char* buf = new char[folder.size];
    unsigned int bytes_read = 0;
    Result r = dataIO.readInodeData(folder, 0, folder.size, &bytes_read, buf);
    if (r != Result::ok || bytes_read != folder.size)
    {
        delete[] buf;
        return r == Result::ok ? Result::bug : r;
    }

    unsigned int p = sizeof(DirEntryCount);  // skip directory entry count
    while (p < folder.size)
    {
        DirEntryLength direntryl = buf[p];
        if (direntryl < sizeof(DirEntryLength) + sizeof(InodeNo))
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "Directory entry size of Folder %u is unplausible! (was: %d, should: >%lu)",
                      folder.no,
                      direntryl,
                      sizeof(DirEntryLength) + sizeof(InodeNo));
            delete[] buf;
            return Result::bug;
        }
        if (direntryl > folder.size)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "BUG: direntry length of Folder %u not plausible (was: %d, should: >%d)!",
                      folder.no,
                      direntryl,
                      folder.size);
            delete[] buf;
            return Result::bug;
        }
        unsigned int dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
        if (dirnamel > folder.size)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "BUG: dirname length of Inode %u not plausible (was: %d, should: >%d)!",
                      folder.no,
                      folder.size,
                      p + dirnamel);
            delete[] buf;
            return Result::bug;
        }
        p += sizeof(DirEntryLength);
        InodeNo tmp_no;
        memcpy(&tmp_no, &buf[p], sizeof(InodeNo));
        p += sizeof(InodeNo);
        char* tmpname = new char[dirnamel + 1];
        memcpy(tmpname, &buf[p], dirnamel);
        tmpname[dirnamel] = 0;
        p += dirnamel;
        if (strcmp(name, tmpname) == 0)
        {
            // Eintrag gefunden
            outInode = tmp_no;
            delete[] tmpname;
            delete[] buf;
            return Result::ok;
        }
        delete[] tmpname;
    }
    delete[] buf;
    return Result::nf;
}

Result
Device::getInodeOfElem(SmartInodePtr& outInode, const char* fullPath)
{
    SmartInodePtr curr;
    if (findOrLoadInode(0, curr) != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not get rootInode! (%s)",
                  resultMsg[static_cast<int>(lasterr)]);
        return Result::fail;
    }

    unsigned int fpLength = strlen(fullPath);
    char* fullPathC = new char[fpLength + 1];
    memcpy(fullPathC, fullPath, fpLength);
    fullPathC[fpLength] = 0;

    char delimiter[] = "/";
    char* fnP;
    fnP = strtok(fullPathC, delimiter);

    while (fnP != nullptr)
    {
        if (strlen(fnP) == 0)
        {  // is first '/'
            continue;
        }

        if (curr->type != InodeType::dir)
        {
            delete[] fullPathC;
            return Result::einval;
        }

        Result r;
        InodeNo next;
        if ((r = getInodeNoInDir(next, *curr, fnP)) != Result::ok)
        {
            delete[] fullPathC;
            // this may be a NotFound
            return r;
        }
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Could not remove Inode Reference!");
            return r;
        }
        r = findOrLoadInode(next, curr);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not get next Inode");
            delete[] fullPathC;
            return r;
        }
        // todo: Dirent cachen
        fnP = strtok(nullptr, delimiter);
    }
    outInode = curr;
    delete[] fullPathC;
    return Result::ok;
}

Result
Device::findOrLoadInode(InodeNo no, SmartInodePtr& target)
{
    Result r = inodePool.getExistingInode(no, target);
    if (r == Result::ok)
    {
        return r;
    }
    r = inodePool.requireNewInode(no, target);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not get new Inode from pool");
        return r;
    }
    r = tree.getInode(no, *target);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not get Inode of elem (%" PRIu32 ")", no);
        return r;
    }
    return Result::ok;
}

Result
Device::insertInodeInDir(const char* name, Inode& contDir, Inode& newElem)
{
    unsigned int elemNameL = strlen(name);
    if (name[elemNameL - 1] == '/')
    {
        elemNameL--;
    }

    if (elemNameL > maxDirEntryLength)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Elem name too long,"
                  " this should have been checked before calling insertInode");
        return Result::objNameTooLong;
    }

    // TODO: Check if name already exists

    DirEntryLength direntryl = sizeof(DirEntryLength) + sizeof(InodeNo)
                               + elemNameL;  // Size of the new directory entry

    unsigned char* buf = new unsigned char[direntryl];
    buf[0] = direntryl;
    memcpy(&buf[sizeof(DirEntryLength)], &newElem.no, sizeof(InodeNo));

    memcpy(&buf[sizeof(DirEntryLength) + sizeof(InodeNo)], name, elemNameL);

    if (contDir.size == 0)
        contDir.size = sizeof(DirEntryCount);  // To hold the Number of Entries

    char* dirData = new char[contDir.size + direntryl];
    unsigned int bytes = 0;
    Result r;
    if (contDir.reservedPages > 0)
    {  // if Directory is not empty
        r = dataIO.readInodeData(contDir, 0, contDir.size, &bytes, dirData);
        if (r != Result::ok || bytes != contDir.size)
        {
            lasterr = r;
            delete[] dirData;
            delete[] buf;
            return r;
        }
    }
    else
    {
        memset(dirData, 0, contDir.size);  // Wipe directory-entry-count area
    }

    // append record
    memcpy(&dirData[contDir.size], buf, direntryl);

    DirEntryCount directoryEntryCount = 0;
    memcpy(&directoryEntryCount, dirData, sizeof(DirEntryCount));
    directoryEntryCount++;
    memcpy(dirData, &directoryEntryCount, sizeof(DirEntryCount));

    // TODO: If write more than one page, split in start and end page to reduce
    // unnecessary writes on intermediate pages.
    r = dataIO.writeInodeData(contDir, 0, contDir.size + direntryl, &bytes, dirData);
    delete[] dirData;
    delete[] buf;
    if (bytes != contDir.size && r == Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "writeInodeData wrote different bytes than requested"
                  ", but returned OK");
    }
    if (r != Result::ok)
    {
        // Ouch, during directory write
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not write %u of %u bytes directory data",
                  contDir.size + direntryl - bytes,
                  contDir.size + direntryl);
        if (bytes != 0)
        {
            Result r2 = tree.updateExistingInode(contDir);
            if (r2 != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not update Inode after unfinished directory insert!"
                          " Ignoring error to continue.");
            }
        }
        return r;
    }

    r = tree.updateExistingInode(contDir);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not update Inode after directory insert!");
        return r;
    }
    return r;
}

// TODO: mark deleted treeCacheNodes as dirty
Result
Device::removeInodeFromDir(Inode& contDir, InodeNo elem)
{
    char* dirData = new char[contDir.size];
    unsigned int bytes = 0;
    Result r;
    if (contDir.reservedPages > 0)
    {  // if Directory is not empty
        r = dataIO.readInodeData(contDir, 0, contDir.size, &bytes, dirData);
        if (r != Result::ok || bytes != contDir.size)
        {
            lasterr = r;
            delete[] dirData;
            return r;
        }
    }
    else
    {
        delete[] dirData;
        return Result::nf;  // did not find directory entry, because dir is empty
    }

    DirEntryCount* entries = reinterpret_cast<DirEntryCount*>(&dirData[0]);
    FileSize pointer = sizeof(DirEntryCount);
    while (pointer < contDir.size)
    {
        DirEntryLength entryl = static_cast<DirEntryLength>(dirData[pointer]);
        if (memcmp(&dirData[pointer + sizeof(DirEntryLength)], &elem, sizeof(InodeNo)) == 0)
        {
            // Found
            unsigned int newSize = contDir.size - entryl;
            unsigned int restByte = newSize - pointer;
            if (newSize == sizeof(DirEntryCount))
                newSize = 0;

            if ((r = dataIO.deleteInodeData(contDir, newSize)) != Result::ok)
            {
                delete[] dirData;
                return r;
            }

            if (restByte > 0 && restByte < 4)  // should either be 0 (no entries left) or bigger
                                               // than 4 (minimum size for one entry)
                PAFFS_DBG(PAFFS_TRACE_BUG, "Something is fishy! (%d)", restByte);

            if (newSize == 0)
            {
                // This was the last entry
                delete[] dirData;
                return dataIO.pac.commit();
            }

            (*entries)--;
            memcpy(&dirData[pointer], &dirData[pointer + entryl], restByte);

            unsigned int bw = 0;
            r = dataIO.writeInodeData(contDir, 0, newSize, &bw, dirData);
            delete[] dirData;
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not update Inode Data");
                return r;
            }
            return dataIO.pac.commit();
        }
        pointer += entryl;
    }
    delete[] dirData;
    return Result::nf;
}

Result
Device::mkDir(const char* fullPath, Permission mask)
{
    if (!mounted)
        return Result::notMounted;
    if (readOnly)
        return Result::readonly;
    if (areaMgmt.getUsedAreas() > areasNo - minFreeAreas)
        return Result::nospace;

    unsigned int lastSlash = 0;

    SmartInodePtr parDir;
    Result res = getParentDir(fullPath, parDir, &lastSlash);
    if (res != Result::ok)
        return res;

    if (strlen(&fullPath[lastSlash]) > maxDirEntryLength)
    {
        return Result::objNameTooLong;
    }

    SmartInodePtr newDir;
    Result r = createDirInode(newDir, mask);
    if (r != Result::ok)
        return r;
    r = tree.insertInode(*newDir);
    if (r != Result::ok)
        return r;

    r = insertInodeInDir(&fullPath[lastSlash], *parDir, *newDir);
    journal.checkpoint();
    return r;
}

Dir*
Device::openDir(const char* path)
{
    if (!mounted)
    {
        lasterr = Result::notMounted;
        return nullptr;
    }
    if (path[0] == 0)
    {
        lasterr = Result::einval;
        return nullptr;
    }

    SmartInodePtr dirPinode;
    Result r = getInodeOfElem(dirPinode, path);
    if (r != Result::ok)
    {
        if (r != Result::nf)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Result::bug? '%s'", err_msg(r));
        }
        lasterr = r;
        return nullptr;
    }

    char* dirData = new char[dirPinode->size];
    unsigned int br = 0;
    if (dirPinode->reservedPages > 0)
    {
        r = dataIO.readInodeData(*dirPinode, 0, dirPinode->size, &br, dirData);
        if (r != Result::ok || br != dirPinode->size)
        {
            lasterr = r;
            return nullptr;
        }
    }
    else
    {
        memset(dirData, 0, dirPinode->size);
    }

    Dir* dir = new Dir;
    dir->self = new Dirent;
    dir->self->name = const_cast<char*>("not_impl.");
    dir->self->node = dirPinode;
    dir->self->no = dirPinode->no;

    dir->self->parent = nullptr;  // no caching, so we probably don't have the parent
    dir->no_entries = dirPinode->size == 0 ? 0 : dirData[0];
    dir->childs = new Dirent[dir->no_entries];
    dir->pos = 0;

    unsigned int p = sizeof(DirEntryCount);
    unsigned int entry;
    for (entry = 0; p < dirPinode->size; entry++)
    {
        DirEntryLength direntryl = dirData[p];
        unsigned int dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
        if (dirnamel > 1 << sizeof(DirEntryLength) * 8)
        {
            // We have an error while reading
            PAFFS_DBG(PAFFS_TRACE_BUG, "Dirname length was bigger than possible (%u)!", dirnamel);
            delete[] dir->childs;
            delete[] dirData;
            delete dir->self;
            delete dir;
            lasterr = Result::bug;
            return nullptr;
        }
        p += sizeof(DirEntryLength);
        memcpy(&dir->childs[entry].no, &dirData[p], sizeof(InodeNo));
        p += sizeof(InodeNo);
        dir->childs[entry].name =
                new char[dirnamel
                         + 2];  //+2 weil 1. Nullbyte und 2. Vielleicht ein Zeichen '/' dazukommt
        memcpy(dir->childs[entry].name, &dirData[p], dirnamel);
        dir->childs[entry].name[dirnamel] = 0;
        dir->childs[entry].parent = dir->self;
        p += dirnamel;
    }

    delete[] dirData;

    if (entry != dir->no_entries)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Directory stated it had %u entries, but has actually %u!",
                  dir->no_entries,
                  entry);
        lasterr = Result::bug;
        return nullptr;
    }

    return dir;
}

Result
Device::closeDir(Dir*& dir)
{
    if (!mounted)
        return Result::notMounted;
    if (dir->childs == nullptr)
        return Result::einval;

    for (int i = 0; i < dir->no_entries; i++)
    {
        delete[] dir->childs[i].name;
    }
    delete[] dir->childs;
    delete dir->self;
    delete dir;
    dir = nullptr;
    return Result::ok;
}

/**
 * TODO: What happens if dir is changed after opendir?
 */
Dirent*
Device::readDir(Dir& dir)
{
    if (!mounted)
    {
        lasterr = Result::notMounted;
        return nullptr;
    }
    if (dir.no_entries == 0)
        return nullptr;

    if (dir.pos == dir.no_entries)
    {
        return nullptr;
    }

    if (dir.pos > dir.no_entries)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir that points further than its contents");
        lasterr = Result::bug;
        return nullptr;
    }

    if (dir.childs[dir.pos].node != nullptr)
    {
        return &dir.childs[dir.pos++];
    }
    Result r = findOrLoadInode(dir.childs[dir.pos].no, dir.childs[dir.pos].node);
    if (r != Result::ok)
    {
        lasterr = Result::bug;
        return nullptr;
    }
    if ((dir.childs[dir.pos].node->perm & R) == 0)
    {
        lasterr = Result::noperm;
        dir.pos++;
        return nullptr;
    }

    if (dir.childs[dir.pos].node->type == InodeType::dir)
    {
        int namel = strlen(dir.childs[dir.pos].name);
        dir.childs[dir.pos].name[namel] = '/';
        dir.childs[dir.pos].name[namel + 1] = 0;
    }
    return &dir.childs[dir.pos++];
}

void
Device::rewindDir(Dir& dir)
{
    dir.pos = 0;
}

Result
Device::createFile(SmartInodePtr& outFile, const char* fullPath, Permission mask)
{
    if (!mounted)
        return Result::notMounted;
    if (readOnly)
        return Result::readonly;
    if (areaMgmt.getUsedAreas() > areasNo - minFreeAreas)
        return Result::nospace;

    unsigned int lastSlash = 0;

    SmartInodePtr parDir;
    Result res = getParentDir(fullPath, parDir, &lastSlash);
    if (res != Result::ok)
    {
        return res;
    }

    if (strlen(&fullPath[lastSlash]) > maxDirEntryLength)
    {
        return Result::objNameTooLong;
    }

    if ((res = createFilInode(outFile, mask)) != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not create fileInode: %s", err_msg(res));
        return res;
    }

    //==== critical zone
    res = tree.insertInode(*outFile);
    if (res != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert Inode into tree: %s", err_msg(res));
        return res;
    }

    res = insertInodeInDir(&fullPath[lastSlash], *parDir, *outFile);
    journal.checkpoint();
    //==================

    return res;
}

Obj*
Device::open(const char* path, Fileopenmask mask)
{
    if (!mounted)
    {
        lasterr = Result::notMounted;
        return nullptr;
    }
    if (readOnly && mask & FW)
    {
        lasterr = Result::readonly;
        return nullptr;
    }
    SmartInodePtr file;
    Result r;
    r = getInodeOfElem(file, path);
    if (r == Result::nf)
    {
        // create new file
        if (mask & FC)
        {
            // use standard mask
            // FIXME: Use standard mask or the mask provided?
            r = createFile(file, path, R | W);
            if (r != Result::ok)
            {
                lasterr = r;
                return nullptr;
            }
        }
        else
        {
            // does not exist, no filecreation bit is given
            lasterr = Result::nf;
            return nullptr;
        }
    }
    else if (r != Result::ok)
    {
        lasterr = r;
        return nullptr;
    }

    if (file->type == InodeType::lnk)
    {
        // LINKS are not supported yet
        lasterr = Result::nimpl;
        return nullptr;
    }

    if (file->type == InodeType::dir)
    {
        // tried to open directory as file
        lasterr = Result::einval;
        return nullptr;
    }

    if ((file->perm | (mask & permMask)) != (file->perm & permMask))
    {
        lasterr = Result::noperm;
        return nullptr;
    }

    Obj* obj;
    lasterr = filesPool.getNewObject(obj);
    if (lasterr != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Too many open files!");
        return nullptr;
    }
    obj->dirent.name = new char[strlen(path)];
    obj->dirent.node = file;
    obj->dirent.no = file->no;
    obj->dirent.parent = nullptr;  // TODO: Sollte aus cache gesucht werden, erstellt in
                                   // "getInodeOfElem(path))" ?

    memcpy(obj->dirent.name, path, strlen(path));

    if (mask & FA)
    {
        obj->fp = file->size;
    }
    else
    {
        obj->fp = 0;
    }

    obj->rdnly = !(mask & FW);
    return obj;
}

Result
Device::close(Obj& obj)
{
    if (!mounted)
        return Result::notMounted;
    Result r = flush(obj);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not flush obj %" PRIu32, obj.dirent.no);
    }

    delete[] obj.dirent.name;
    filesPool.freeObject(obj);
    return r;
}

Result
Device::touch(const char* path)
{
    if (!mounted)
        return Result::notMounted;
    if (readOnly)
        return Result::readonly;
    if (areaMgmt.getUsedAreas() > areasNo - minFreeAreas)
        // If we use reserved Areas, extensive touching may fill flash anyway
        return Result::nospace;

    SmartInodePtr file;
    Result r = getInodeOfElem(file, path);
    if (r == Result::nf)
    {
        // create new file
        Result r2 = createFile(file, path, R | W);
        if (r2 != Result::ok)
        {
            return r2;
        }
        return Result::ok;
    }
    else
    {
        if (r != Result::ok)
            return r;
        file->mod = systemClock.now()
                            .convertTo<outpost::time::GpsTime>()
                            .timeSinceEpoch()
                            .milliseconds();
        r = tree.updateExistingInode(*file);
        journal.checkpoint();
        return r;
    }
}

Result
Device::getObjInfo(const char* fullPath, ObjInfo& nfo)
{
    if (!mounted)
        return Result::notMounted;
    Result r;
    SmartInodePtr object;
    if ((r = getInodeOfElem(object, fullPath)) != Result::ok)
    {
        return lasterr = r;
    }
    nfo.created = outpost::time::GpsTime::afterEpoch(outpost::time::Milliseconds(object->crea));
    nfo.modified = outpost::time::GpsTime::afterEpoch(outpost::time::Milliseconds(object->crea));
    ;
    nfo.perm = object->perm;
    nfo.size = object->size;
    nfo.isDir = object->type == InodeType::dir;
    return Result::ok;
}

Result
Device::read(Obj& obj, char* buf, unsigned int bytes_to_read, unsigned int* bytes_read)
{
    if (!mounted)
        return Result::notMounted;
    if (obj.dirent.node->type == InodeType::dir)
    {
        return lasterr = Result::einval;
    }
    if (obj.dirent.node->type == InodeType::lnk)
    {
        return lasterr = Result::nimpl;
    }
    if ((obj.dirent.node->perm & R) == 0)
        return Result::noperm;

    if (obj.dirent.node->size == 0)
    {
        *bytes_read = 0;
        return Result::ok;
    }

    Result r = dataIO.readInodeData(*obj.dirent.node, obj.fp, bytes_to_read, bytes_read, buf);
    if (r != Result::ok)
    {
        return r;
    }

    //*bytes_read = bytes_to_read;
    obj.fp += *bytes_read;
    return Result::ok;
}

Result
Device::write(Obj& obj, const char* buf, unsigned int bytes_to_write, unsigned int* bytes_written)
{
    *bytes_written = 0;
    if (!mounted)
        return Result::notMounted;
    if (readOnly)
        return Result::readonly;
    if (areaMgmt.getUsedAreas() > areasNo - minFreeAreas)
        return Result::nospace;
    if (obj.dirent.node == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Objects dirent.node is invalid!");
        return Result::bug;
    }

    if (obj.dirent.node->type == InodeType::dir)
        return Result::einval;
    if (obj.dirent.node->type == InodeType::lnk)
        return Result::nimpl;
    if ((obj.dirent.node->perm & W) == 0)
        return Result::noperm;

    Result r = dataIO.writeInodeData(*obj.dirent.node, obj.fp, bytes_to_write, bytes_written, buf);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not write %u of %u bytes",
                  bytes_to_write - *bytes_written,
                  bytes_to_write);
        if (*bytes_written > 0)
        {
            Result r2 = tree.updateExistingInode(*obj.dirent.node);
            if (r2 != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "could not update Inode of unsuccessful inode write "
                          "(%s)",
                          err_msg(r2));
            }
        }
        return r;
    }
    if (*bytes_written != bytes_to_write)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not write Inode Data in whole! (Should: %d, was: %d)",
                  bytes_to_write,
                  *bytes_written);
        // TODO: Handle error, maybe rewrite
        return Result::fail;
    }

    obj.dirent.node->mod =
            systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();

    obj.fp += *bytes_written;
    if (obj.fp > obj.dirent.node->size)
    {
        // size was increased
        if (obj.dirent.node->reservedPages * dataBytesPerPage < obj.fp)
        {
            PAFFS_DBG(PAFFS_TRACE_WRITE,
                      "Reserved size is smaller than actual size "
                      "which is OK if we skipped pages");
        }
    }
    r = tree.updateExistingInode(*obj.dirent.node);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not update existing inode");
        return r;
    }
    journal.checkpoint();
    return r;
}

Result
Device::seek(Obj& obj, int m, Seekmode mode)
{
    if (!mounted)
        return Result::notMounted;
    switch (mode)
    {
    case Seekmode::set:
        if (m < 0)
            return lasterr = Result::einval;
        obj.fp = m;
        break;
    case Seekmode::end:
        if (-m > static_cast<int>(obj.dirent.node->size))
            return Result::einval;
        obj.fp = obj.dirent.node->size + m;
        break;
    case Seekmode::cur:
        if (static_cast<int>(obj.fp) + m < 0)
            return Result::einval;
        obj.fp += m;
        break;
    }

    return Result::ok;
}

Result
Device::flush(Obj& obj)
{
    if (!mounted)
        return Result::notMounted;
    if (readOnly)
        return Result::ok;

    // TODO: When Inodes get Link to its PAC, this would be more elegant
    Result r = dataIO.pac.setTargetInode(*obj.dirent.node);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Target Inode!");
        return r;
    }
    dataIO.pac.commit();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Inode!");
        return r;
    }
    journal.checkpoint();
    return Result::ok;
}

Result
Device::truncate(const char* path, unsigned int newLength)
{
    if (!mounted)
        return Result::notMounted;
    if (readOnly)
        return Result::readonly;

    SmartInodePtr object;
    Result r;
    if ((r = getInodeOfElem(object, path)) != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not get Inode of elem");
        return r;
    }

    if (!(object->perm & W))
    {
        return Result::noperm;
    }

    if (object->type == InodeType::dir)
    {
        if (object->size > sizeof(DirEntryCount))
        {
            return Result::dirnotempty;
        }
    }

    r = dataIO.deleteInodeData(*object, newLength);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete Inode Data");
        return r;
    }
    r = dataIO.pac.commit();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit PAC");
    }
    journal.checkpoint();
    return r;
}

Result
Device::remove(const char* path)
{
    Result r = truncate(path, 0);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete the files contents");
        return r;
    }
    SmartInodePtr object;
    if ((r = getInodeOfElem(object, path)) != Result::ok)
        return r;

    // If reference count is bigger than our own reference
    if (inodePool.map[object->no].second > 1)
    {
        // Still opened by others
        return Result::einval;
    }

    SmartInodePtr parentDir;
    unsigned int lastSlash = 0;
    if ((r = getParentDir(path, parentDir, &lastSlash)) != Result::ok)
        return r;

    if ((r = removeInodeFromDir(*parentDir, object->no)) != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not remove InodeNo from Dir");
        return r;
    }
    r = tree.deleteInode(object->no);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete Inode");
        return r;
    }
    journal.checkpoint();
    return Result::ok;
}

Result
Device::chmod(const char* path, Permission perm)
{
    if (!mounted)
        return Result::notMounted;
    Result r;
    SmartInodePtr object;
    if ((r = getInodeOfElem(object, path)) != Result::ok)
    {
        return r;
    }
    object->perm = perm;
    r = tree.updateExistingInode(*object);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not update Inode");
        return r;
    }
    journal.checkpoint();
    return r;
}

Result
Device::getListOfOpenFiles(Obj* list[])
{
    unsigned int pos = 0;
    for (unsigned int i = 0; i < maxNumberOfFiles; i++)
    {
        if (filesPool.activeObjects.getBit(i))
        {
            list[pos++] = &filesPool.objects[i];
        }
    }
    return Result::ok;
}

uint8_t
Device::getNumberOfOpenFiles()
{
    return filesPool.getUsage();
}

uint8_t
Device::getNumberOfOpenInodes()
{
    return inodePool.getUsage();
}

Result
Device::initializeDevice()
{
    if (mounted)
        return Result::alrMounted;
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Device is not yet mounted");

    areaMgmt.clear();

    if (areasNo < AreaType::no - 2)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Device too small, at least %u Areas are needed!",
                  static_cast<unsigned>(AreaType::no));
        return Result::einval;
    }

    if (blocksPerArea < 2)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Device too small, at least 2 Blocks per Area are needed!");
        return Result::einval;
    }

    if (dataPagesPerArea > dataBytesPerPage * 8)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Device Areas too big, Area Summary would not fit a single page!");
        return Result::einval;
    }

    if (blocksTotal % blocksPerArea != 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "'blocksPerArea' does not divide "
                  "%u blocks evenly! (define %u, rest: %u)",
                  blocksTotal,
                  blocksPerArea,
                  blocksTotal % blocksPerArea);
        return Result::einval;
    }
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Init success");
    return Result::ok;
}

Result
Device::destroyDevice()
{
    areaMgmt.clear();
    inodePool.clear();
    filesPool.clear();
    return Result::ok;
}
};
