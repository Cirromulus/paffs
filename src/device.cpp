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
#include <memory>

namespace paffs
{
outpost::rtos::SystemClock systemClock;

Device::Device(Driver& _driver) :
      driver(_driver),
      lasterr(Result::ok),
      mounted(false),
      readOnly(false),
      tree(this),
      sumCache(this),
      areaMgmt(this),
      dataIO(this),
      superblock(this),
      journalPersistence(this),
      journal(journalPersistence, superblock, sumCache, tree,
              dataIO, dataIO.pac, *this){};

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
    {
        return Result::alrMounted;
    }

    journal.disable();

    Result r = initializeDevice();
    if (r != Result::ok)
    {
        return r;
    }
    r = driver.initializeNand();
    if (r != Result::ok)
    {
        return r;
    }

    if (complete)
    {
        PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Deleting all areas.\n");
    }

    BitList<AreaType::no> hadAreaType;
    uint8_t hadSuperblocks = 0;

    for (BlockAbs block = 0; block < badBlockList.mSize; block++)
    {
        AreaPos area = badBlockList[block] / blocksPerArea;
        PAFFS_DBG_S(
                PAFFS_TRACE_BAD_BLOCKS, "Retiring Area %" PTYPE_AREAPOS " because of given List", area);

        if (badBlockList[block] > blocksTotal)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Invalid Bad Block given! "
                      "was %" PTYPE_BLOCKABS " area %" PTYPE_AREAPOS ", should < %" PTYPE_BLOCKABS,
                      badBlockList[block],
                      area,
                      blocksTotal);
            return Result::invalidInput;
        }
        if (area == 0)
        {
            // First and reserved Area
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Bad block in reserved first Area!");
            return Result::fail;
        }
        if(superblock.getType(area) != AreaType::retired)
        {
            superblock.setPos(area, area);
            areaMgmt.retireArea(area);
        }
    }

    for (AreaPos area = 0; area < areasNo; area++)
    {
        if (superblock.getType(area) == AreaType::retired)
        {
            continue;
        }

        superblock.setStatus(area, AreaStatus::empty);
        // erasecount is already set to 0
        superblock.setPos(area, area);

        bool anyBlockInAreaBad = false;
        for (BlockAbs block = 0; block < blocksPerArea; block++)
        {
            if (driver.checkBad(area * blocksPerArea + block) != Result::ok)
            {
                PAFFS_DBG_S(PAFFS_TRACE_BAD_BLOCKS,
                            "Found marked bad block %" PTYPE_BLOCKABS " during formatting, "
                            "retiring area %" PTYPE_AREAPOS,
                            area * blocksPerArea + block,
                            area);
                anyBlockInAreaBad = true;
            }
        }
        if (anyBlockInAreaBad)
        {
            areaMgmt.retireArea(area);
            continue;
        }

        if (complete ||
                !(hadAreaType.getBit(AreaType::superblock) &&
                  hadAreaType.getBit(AreaType::garbageBuffer)))
        {
            for (BlockAbs p = 0; p < blocksPerArea; p++)
            {
                r = driver.eraseBlock(p + area * blocksPerArea);
                if (r != Result::ok)
                {
                    PAFFS_DBG_S(PAFFS_TRACE_BAD_BLOCKS,
                                "Found non-marked bad block %" PTYPE_BLOCKABS " during formatting, "
                                "retiring area %" PTYPE_AREAPOS,
                                p + area * blocksPerArea,
                                area);
                    areaMgmt.retireArea(area);
                    break;
                }
            }
            superblock.increaseErasecount(area);

            if (superblock.getType(area) == AreaType::retired)
            {
                continue;
            }
        }

        if (!hadAreaType.getBit(AreaType::superblock))
        {
            areaMgmt.initAreaAs(area, AreaType::superblock);
            superblock.setActiveArea(AreaType::superblock, 0);
            if (++hadSuperblocks == superChainElems)
            {
                hadAreaType.setBit(AreaType::superblock);
            }
            continue;
        }

        if(useJournal)
        {
            if(!hadAreaType.getBit(AreaType::journal))
            {
                areaMgmt.initAreaAs(area, AreaType::journal);
                hadAreaType.setBit(AreaType::journal);
                continue;
            }
        }

        if (!hadAreaType.getBit(AreaType::garbageBuffer))
        {
            areaMgmt.initAreaAs(area, AreaType::garbageBuffer);
            hadAreaType.setBit(AreaType::garbageBuffer);
            continue;
        }

        superblock.setType(area, AreaType::unset);
    }

    r = tree.startNewTree();
    if (r != Result::ok)
    {
        return r;
    }
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

    journal.enable();
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
    {
        return Result::alrMounted;
    }
    journal.disable();
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
    if (r == Result::notFound)
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
    //Basic sanity check
    if(rootDir->type != InodeType::dir)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Root directory is not marked as dir");
        printf("Inode %" PTYPE_INODENO " : %s\n", rootDir->no,
               rootDir->type == InodeType::file ? "fil" : "dir");
        printf("   Perm: %s%s%s\n", rootDir->perm & R ? "r" : "-",
                rootDir->perm & W ? "w" : "-",
                        rootDir->perm & X ? "x" : "-"
                );
        printf("   Size: %" PTYPE_FILSIZE " Byte (%" PRIu32 " pages)\n",
               rootDir->size, rootDir->reservedPages);
        for(uint8_t i = 0; i < directAddrCount + 3; i++)
        {   //Intentionally reading over boundary of direct array into indirect
            if(rootDir->direct[i] == 0)
            {
                printf("   ...\n");
                break;
            }
            printf("   %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS "\n",
                   extractLogicalArea(rootDir->direct[i]), extractPageOffs(rootDir->direct[i]));
        }
        return Result::fail;
    }

    if((traceMask & PAFFS_TRACE_VERIFY_DEV) && checkFolderSanity(0) != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Rootfolder not sane!");
        return Result::fail;
    }

    if (traceMask & PAFFS_TRACE_AREA)
    {
        debugPrintStatus();
    }

    // TODO: Supress decrease or increase reference to node 0 manually
    mounted = true;
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Mount successful");
    return r;
}

Result
Device::flushAllCaches()
{
    if (!mounted)
     {
         return Result::notMounted;
     }
     Result r;

     InodePool<maxNumberOfInodes>::InodeMap::iterator it = inodePool.map.begin();
     if (it != inodePool.map.end())
     {
         while (it != inodePool.map.end())
         {
             PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                         "Commit Inode %" PTYPE_INODENO " with %" PRIu8 " references",
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
             it++;
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
         debugPrintStatus();
     }

     journal.clear();

     return Result::ok;
}

Result
Device::unmnt()
{
    Result r = flushAllCaches();
    if(r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not flush caches for unmount");
        return r;
    }

    destroyDevice();
    driver.deInitializeNand();
    tree.wipeCache();   // just for cleanup & tests
    mounted = false;
    return Result::ok;
}

void
Device::debugPrintStatus()
{
    TraceMask bkp = traceMask;
    traceMask &= ~PAFFS_TRACE_ASCACHE;
    printf("Info: \n\t%" PTYPE_AREAPOS " used Areas\n", superblock.getUsedAreas());
    for (AreaPos i = 0; i < areasNo; i++)
    {
        SummaryEntry summary[dataPagesPerArea];
        sumCache.getEstimatedSummaryStatus(i, summary);
        PageOffs dirtyPages = 0;
        PageOffs freePages = 0;
        for(PageOffs p = 0; p < dataPagesPerArea; p++)
        {
            if(summary[p] == SummaryEntry::dirty)
            {
                dirtyPages++;
            }
            if(summary[p] == SummaryEntry::free)
            {
                freePages++;
            }
        }
        printf("\tArea %03" PTYPE_AREAPOS " on %03" PTYPE_AREAPOS " as %6s "
                "(%3" PTYPE_PAGEOFFS "/%3" PTYPE_PAGEOFFS " dirty/free) %s\n",
               i,
               superblock.getPos(i),
               areaNames[superblock.getType(i)],
               dirtyPages, freePages,
               areaStatusNames[superblock.getStatus(i)]);
        if (i > 128)
        {
            printf("\n -- truncated 128-%" PTYPE_AREAPOS " Areas.\n", areasNo);
            break;
        }
    }
    printf("\t----------------------\n");
    traceMask = bkp;
}

Result
Device::createInode(SmartInodePtr& outInode, Permission mask)
{
    // FIXME: is this the best way to find a new number?
    InodeNo no;
    Result r = tree.findFirstFreeNo(&no);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find a free Inode Number");
        return r;
    }
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
    {
        return Result::bug;
    }
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
Device::getParentDir(const char* fullPath, SmartInodePtr& parDir, FileNamePos* lastSlash)
{
    if (fullPath[0] == 0)
    {
        lasterr = Result::invalidInput;
        return Result::invalidInput;
    }

    FileNamePos p = 0;
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
        // Just contains a zero for "No entries"
        return Result::notFound;
    }
    DirEntryLength nameLength = strlen(name);
    std::unique_ptr<uint8_t[]> buf(new uint8_t[folder.size]);
    FileSize bytesRead = 0;
    Result r = dataIO.readInodeData(folder, 0, folder.size, &bytesRead, buf.get());
    if (r != Result::ok || bytesRead != folder.size)
    {
        return r == Result::ok ? Result::bug : r;
    }


    DirEntryCount dirEntries;
    memcpy(&dirEntries, buf.get(), sizeof(DirEntryCount));
    FileSize p = sizeof(DirEntryCount);
    DirEntryCount entryNo = 0;

    PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                "Searching for '%s' in Inode %" PTYPE_INODENO " (%" PTYPE_DIRENTRYCOUNT " entries)",
                name, folder.no, dirEntries);

    while (p < folder.size)
    {
        DirEntryLength direntryl = buf[p];
        if (direntryl < sizeof(DirEntryLength) + sizeof(InodeNo) + 1)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "Folder %" PTYPE_INODENO ": Directory entry %" PTYPE_DIRENTRYCOUNT " size is unplausible! "
                              "(was: %" PTYPE_DIRENTRYLEN ", should: >%zu)",
                      folder.no, entryNo, direntryl,
                      sizeof(DirEntryLength) + sizeof(InodeNo) + 1);
            return Result::bug;
        }
        if (direntryl > folder.size)
        {
            if(true || (traceMask & PAFFS_TRACE_VERBOSE))
            {
                for(uint16_t i = 0; i < folder.size; i+=4)
                {
                    printf("%3u-%3u: %3u %3u %3u %3u | ",
                            i, i+3,
                            buf[i], buf[i+1], buf[i+2], buf[i+3]);
                    for(uint8_t ch = 0; ch < 4; ch++)
                    {
                        if(buf[i + ch] > 31 && buf[i + ch] < 127)
                        {
                            printf("%c", buf[i + ch]);
                        }else
                        {
                            printf(" ");
                        }
                    }
                    if(i >= p-4 && i < p)
                    {
                        printf(" <");
                    }
                    printf("\n");
                }
            }
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "Folder %" PTYPE_INODENO ": Directory entry %" PTYPE_DIRENTRYCOUNT " "
                              "(%" PTYPE_FILSIZE ") length not plausible "
                              "(was: %" PTYPE_DIRENTRYLEN ", should: <%" PTYPE_FILSIZE ")!",
                      folder.no, entryNo, p,  direntryl,
                      folder.size);
            return Result::bug;
        }
        FileNamePos dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
        p += sizeof(DirEntryLength);
        p += sizeof(InodeNo);
        if ((dirnamel == nameLength) && (memcmp(name, &buf[p], dirnamel) == 0))
        {
            // Eintrag gefunden
            memcpy(&outInode, &buf[p - sizeof(InodeNo)], sizeof(InodeNo));
            PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                        "Found '%.*s' with Inode %" PTYPE_INODENO " at offs %" PTYPE_FILSIZE,
                        dirnamel, &buf[p], outInode,
                        static_cast<FileSize>(p - sizeof(InodeNo) - sizeof(DirEntryLength)));
            return Result::ok;
        }
        if(traceMask & PAFFS_TRACE_VERBOSE)
        {
            memcpy(&outInode, &buf[p - sizeof(InodeNo)], sizeof(InodeNo));
            PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                        "Not '%.*s' (length %" PTYPE_DIRENTRYLEN ") with Inode %" PTYPE_INODENO " at offs %" PTYPE_FILSIZE,
                        dirnamel, &buf[p], direntryl, outInode,
                        static_cast<FileSize>(p - sizeof(InodeNo) - sizeof(DirEntryLength)));
        }
        p += dirnamel;
        entryNo++;
    }
    return Result::notFound;
}

Result
Device::getNameOfInodeInDir(InodeNo target, Inode& folder, char* outName, uint8_t &namelength)
{
    //FIXME: Copypaste of InodeNoInDir, create a 'FolderTraverser' that does both
    if (folder.type != InodeType::dir)
    {
        return Result::bug;
    }
    if (folder.size <= sizeof(DirEntryCount))
    {
        // Just contains a zero for "No entries"
        return Result::notFound;
    }
    std::unique_ptr<uint8_t[]> buf(new uint8_t[folder.size]);
    FileSize bytesRead = 0;
    Result r = dataIO.readInodeData(folder, 0, folder.size, &bytesRead, buf.get());
    if (r != Result::ok || bytesRead != folder.size)
    {
        return r == Result::ok ? Result::bug : r;
    }


    DirEntryCount dirEntries;
    memcpy(&dirEntries, buf.get(), sizeof(DirEntryCount));
    FileSize p = sizeof(DirEntryCount);
    DirEntryCount entryNo = 0;

    PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                "Searching for Inode %" PTYPE_INODENO " in Inode %" PTYPE_INODENO " (%" PTYPE_DIRENTRYCOUNT " entries)",
                target, folder.no, dirEntries);

    while (p < folder.size)
    {
        DirEntryLength direntryl = buf[p];
        if (direntryl < sizeof(DirEntryLength) + sizeof(InodeNo) + 1)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "Folder %" PTYPE_INODENO ": Directory entry %" PTYPE_DIRENTRYCOUNT " size is unplausible! "
                              "(was: %" PTYPE_DIRENTRYLEN ", should: >%zu)",
                      folder.no, entryNo, direntryl,
                      sizeof(DirEntryLength) + sizeof(InodeNo) + 1);
            return Result::bug;
        }
        if (direntryl > folder.size)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "Folder %" PTYPE_INODENO ": Directory entry %" PTYPE_DIRENTRYCOUNT " length not plausible "
                              "(was: %" PTYPE_DIRENTRYLEN ", should: >%" PTYPE_FILSIZE ")!",
                      folder.no, entryNo, direntryl,
                      folder.size);
            return Result::bug;
        }
        FileNamePos dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
        p += sizeof(DirEntryLength);
        InodeNo elem;
        memcpy(&elem, &buf[p], sizeof(InodeNo));
        p += sizeof(InodeNo);
        if (elem == target)
        {
            // Eintrag gefunden
            memcpy(&outName, &buf[p], dirnamel < namelength ?  dirnamel : namelength);
            namelength = dirnamel;
            PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                        "Found '%.*s' with Inode %" PTYPE_INODENO " at offs %" PTYPE_FILSIZE,
                        dirnamel, &buf[p], elem,
                        static_cast<FileSize>(p - sizeof(InodeNo) - sizeof(DirEntryLength)));
            return Result::ok;
        }
        if(traceMask & PAFFS_TRACE_VERBOSE)
        {
            PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                        "Not '%.*s' (length %" PTYPE_DIRENTRYLEN ") with Inode %" PTYPE_INODENO " at offs %" PTYPE_FILSIZE,
                        dirnamel, &buf[p], direntryl, elem,
                        static_cast<FileSize>(p - sizeof(InodeNo) - sizeof(DirEntryLength)));
        }
        p += dirnamel;
        entryNo++;
    }
    return Result::notFound;
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

    FileNamePos fpLength = strlen(fullPath);
    char fullPathC[fpLength + 1];
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
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Inode %" PTYPE_INODENO " is not a directory!", curr->no);
            return Result::invalidInput;
        }

        Result r;
        InodeNo next;
        if ((r = getInodeNoInDir(next, *curr, fnP)) != Result::ok)
        {
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
            return r;
        }
        // todo: Dirent cachen
        fnP = strtok(nullptr, delimiter);
    }
    outInode = curr;
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
    journal.addEvent(journalEntry::device::InsertIntoDir(contDir.no));
    FileNamePos elemNameL = strlen(name);
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

    uint8_t buf[direntryl];
    buf[0] = direntryl;
    memcpy(&buf[sizeof(DirEntryLength)], &newElem.no, sizeof(InodeNo));

    memcpy(&buf[sizeof(DirEntryLength) + sizeof(InodeNo)], name, elemNameL);

    if (contDir.size == 0)
    {
        contDir.size = sizeof(DirEntryCount);  // To hold the Number of Entries
    }

    std::unique_ptr<uint8_t[]> dirData(new uint8_t[contDir.size + direntryl]);
    FileSize bytes = 0;
    Result r;
    if (contDir.reservedPages > 0)
    {  // if Directory is not empty
        r = dataIO.readInodeData(contDir, 0, contDir.size, &bytes, dirData.get());
        if (r != Result::ok || bytes != contDir.size)
        {
            lasterr = r;
            return r;
        }
    }
    else
    {
        memset(dirData.get(), 0, contDir.size);  // Wipe directory-entry-count area
    }

    // append record
    memcpy(&dirData[contDir.size], buf, direntryl);
    DirEntryCount directoryEntryCount = 0;
    memcpy(&directoryEntryCount, dirData.get(), sizeof(DirEntryCount));
    directoryEntryCount++;
    memcpy(dirData.get(), &directoryEntryCount, sizeof(DirEntryCount));

    PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                "Appending '%.*s' to Inode %" PTYPE_INODENO " at %" PTYPE_FILSIZE
                " (now %" PTYPE_DIRENTRYCOUNT " entries)",
                elemNameL, name, contDir.no, contDir.size, directoryEntryCount);

    // TODO: If write more than one page, split in start and end page to reduce
    // unnecessary writes on intermediate pages.
    r = dataIO.writeInodeData(contDir, 0, contDir.size + direntryl, &bytes, dirData.get());
    dirData.reset();
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
                  "Could not write %" PRIu32 " of %" PRIu32 " bytes directory data",
                  contDir.size + direntryl - bytes,
                  contDir.size + direntryl);
        return r;
    }
    contDir.mod =
                systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();
    tree.updateExistingInode(contDir);
    journal.addEvent(journalEntry::Checkpoint(JournalEntry::Topic::dataIO));
    Result journalStatus = journal.addEvent(journalEntry::Checkpoint(getTopic()));

    //FIXME Dirty hack. Actually, the Problem is with the inode reference.
    //      Inode target is located in Tree, which may have cleared the node until we commit PAC.
    //      This may be fixed either by locking the Inode (potentially filling cache, bad)
    //      Or by committing PAC as soon as it gets deleted (Add pac reference to Inode)

    //PAC commit also updates tree
    r = dataIO.pac.commit();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not update Inode after directory insert!");
        return r;
    }
    if(traceMask & PAFFS_TRACE_VERIFY_DEV)
    {
        r = checkFolderSanity(contDir.no);
    }
    if(journalStatus == Result::lowMem)
    {
        return flushAllCaches();
    }
    return r;
}

Result
Device::checkFolderSanity(InodeNo folderNo)
{
    SmartInodePtr folder;
    Result r = findOrLoadInode(folderNo, folder);
    if(r != Result::ok)
    {
        return r;
    }
    std::unique_ptr<uint8_t[]> dirData(new uint8_t[folder->size]);
    FileSize bytes = 0;
    if (folder->reservedPages == 0)
    {
        return Result::ok;
    }
    r = dataIO.readInodeData(*folder, 0, folder->size, &bytes, dirData.get());
    if (r != Result::ok || bytes != folder->size)
    {
        return r;
    }
    DirEntryCount noEntries;
    memcpy(&noEntries, &dirData[0], sizeof(DirEntryCount));
    FileSize p = sizeof(DirEntryCount);
    DirEntryCount currentEntry = 0;
    while (p < folder->size)
    {
        DirEntryLength direntryl = dirData[p];
        if (direntryl > folder->size - p)
        {
            // We have an error while reading
            PAFFS_DBG(PAFFS_TRACE_BUG, "Dirname length was bigger than possible "
                    "(%" PTYPE_FILNAMEPOS " > %" PTYPE_FILNAMEPOS ")!",
                    direntryl, folder->size - p);
            return Result::bug;
        }
        FileNamePos dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
        p += sizeof(DirEntryLength);
        Inode obj;
        InodeNo objNo;
        memcpy(&objNo, &dirData[p], sizeof(InodeNo));
        r = tree.getInode(objNo, obj);
        if(r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Folder contains a nonexistent Inode %" PTYPE_INODENO, objNo);
            return Result::bug;
        }
        p += sizeof(InodeNo);
        for(uint8_t i = 0; i < dirnamel; i++)
        {
            if(dirData[p+i] < 32 || dirData[p+i] > 126)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG, "Directory %" PTYPE_INODENO " contains "
                        "non-printable ascii character at offs %" PTYPE_FILSIZE "!",
                        folder->no, p+i);
                return Result::bug;
            }
        }
        if(traceMask & PAFFS_TRACE_VERBOSE)
        {
            char name[dirnamel];
            memcpy(name, &dirData[p], dirnamel);
            //printf("%3u: %3" PTYPE_INODENO ", %.*s\n", currentEntry + 1, 0, dirnamel, name);
        }
        p += dirnamel;
        currentEntry++;
    }

    if (currentEntry != noEntries)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Directory stated it had %" PTYPE_DIRENTRYCOUNT " entries, "
                          "but has actually %" PTYPE_DIRENTRYCOUNT "!",
                  noEntries, currentEntry);
        return Result::bug;
    }

    return Result::ok;
}

Result
Device::removeInodeFromDir(Inode& contDir, InodeNo elem)
{
    std::unique_ptr<uint8_t[]> dirData(new uint8_t[contDir.size]);
    FileSize bytes = 0;
    Result r;
    if (contDir.reservedPages > 0)
    {  // if Directory is not empty
        r = dataIO.readInodeData(contDir, 0, contDir.size, &bytes, dirData.get());
        if (r != Result::ok || bytes != contDir.size)
        {
            lasterr = r;
            return r;
        }
    }
    else
    {
        return Result::notFound;  // did not find directory entry, because dir is empty
    }

    DirEntryCount entries = 0;
    memcpy(&entries, &dirData[0], sizeof(DirEntryCount));
    FileSize pointer = sizeof(DirEntryCount);

    PAFFS_DBG_S(PAFFS_TRACE_DEVICE,
                "Deleting %" PTYPE_INODENO " from Inode %" PTYPE_INODENO " (%" PTYPE_DIRENTRYCOUNT " entries)",
                elem, contDir.no, entries);

    while (pointer < contDir.size)
    {
        DirEntryLength entryl = static_cast<DirEntryLength>(dirData[pointer]);
        if (memcmp(&dirData[pointer + sizeof(DirEntryLength)], &elem, sizeof(InodeNo)) == 0)
        {
            // Found
            PAFFS_DBG_S(PAFFS_TRACE_DEVICE, "Found entry '%.*s' at offs %" PTYPE_FILSIZE,
                        static_cast<int>(entryl - sizeof(InodeNo) - 1),
                        &dirData[pointer + sizeof(DirEntryLength) + sizeof(InodeNo)],
                        pointer);

            FileSize newSize = contDir.size - entryl;
            FileSize restByte = newSize - pointer;
            if (newSize == sizeof(DirEntryCount))
            {
                newSize = 0;
            }

            if (restByte > 0 && restByte < 4)  // should either be 0 (no entries left) or bigger
            {                                  // than 4 (minimum size for one entry)
                PAFFS_DBG(PAFFS_TRACE_BUG, "Something is fishy! (%" PTYPE_FILSIZE ")", restByte);
            }
            if (newSize == 0)
            {
                // This was the last entry
                if ((r = dataIO.deleteInodeData(contDir, 0)) != Result::ok)
                {
                    return r;
                }
                return dataIO.pac.commit();
            }

            PAFFS_DBG_S(PAFFS_TRACE_DEVICE, "Slicing folder from %" PTYPE_FILSIZE " "
                    "to %" PTYPE_FILSIZE " at %" PTYPE_FILSIZE " (moving %" PTYPE_FILSIZE " Bytes to left)",
                    contDir.size, newSize, pointer, restByte);

            entries--;
            memcpy(&dirData[0], &entries, sizeof(DirEntryCount));
            //source and destination may overlap if there is more than one entry behind us
            memmove(&dirData[pointer], &dirData[pointer + entryl], restByte);

            //This lets journal know what to do if write succeeded
            journal.addEvent(journalEntry::dataIO::NewInodeSize(contDir.no, newSize));
            FileSize bw = 0;
            r = dataIO.writeInodeData(contDir, 0, newSize, &bw, dirData.get());
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not update Inode Data");
                return r;
            }

            if ((r = dataIO.deleteInodeData(contDir, newSize)) != Result::ok)
            {
                return r;
            }
            //DeleteInodeData updated Inode in tree and also wrote checkpoint

            r = dataIO.pac.commit();
            if(r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit tuncated folder %" PTYPE_INODENO,
                          contDir.no);
                return r;
            }

            if (traceMask & PAFFS_TRACE_VERIFY_DEV)
            {
                r = checkFolderSanity(contDir.no);
                if(r != Result::ok)
                {
                    return r;
                }
            }
            r = journal.addEvent(journalEntry::Checkpoint(getTopic()));
            if(r == Result::lowMem)
            {
                PAFFS_DBG(PAFFS_TRACE_DEVICE, "Journal nearly full, flushing caches");
                return flushAllCaches();
            }
            return Result::ok;
        }
        pointer += entryl;
    }
    return Result::notFound;
}

Result
Device::mkDir(const char* fullPath, Permission mask)
{
    if (!mounted)
    {
        return Result::notMounted;
    }
    if (readOnly)
    {
        return Result::readOnly;
    }
    if (superblock.getUsedAreas() > areasNo - minFreeAreas)
    {
        return Result::noSpace;
    }

    FileNamePos lastSlash = 0;

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


    SmartInodePtr newDir;
    Result r = createDirInode(newDir, mask);
    if (r != Result::ok)
    {
        return r;
    }
    journal.addEvent(journalEntry::device::MkObjInode(newDir->no));
    r = tree.insertInode(*newDir);
    if (r != Result::ok)
    {
        return r;
    }
    //Journal log is included in insertInodeInDir
    r = insertInodeInDir(&fullPath[lastSlash], *parDir, *newDir);
    if(r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new Dir inode in parent dir");
        return r;
    }
    r = journal.addEvent(journalEntry::Checkpoint(getTopic()));
    if(r == Result::lowMem)
    {
        PAFFS_DBG(PAFFS_TRACE_DEVICE, "Journal nearly full, flushing caches");
        return flushAllCaches();
    }
    return Result::ok;
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
        lasterr = Result::invalidInput;
        return nullptr;
    }

    SmartInodePtr dirPinode;
    Result r = getInodeOfElem(dirPinode, path);
    if (r != Result::ok)
    {
        if (r != Result::notFound)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Result::bug? '%s'", err_msg(r));
        }
        lasterr = r;
        return nullptr;
    }
    std::unique_ptr<uint8_t[]> dirData(new uint8_t[dirPinode->size]);
    FileSize br = 0;
    if (dirPinode->reservedPages > 0)
    {
        r = dataIO.readInodeData(*dirPinode, 0, dirPinode->size, &br, dirData.get());
        if (r != Result::ok || br != dirPinode->size)
        {
            lasterr = r;
            return nullptr;
        }
    }
    else
    {
        memset(dirData.get(), 0, dirPinode->size);
    }

    Dir* dir = new Dir;
    dir->self = new Dirent;
    dir->self->name = const_cast<char*>("not_impl.");
    dir->self->node = dirPinode;
    dir->self->no = dirPinode->no;

    dir->self->parent = nullptr;  // no caching, so we probably don't have the parent
    if(dirPinode->size == 0)
    {
        dir->entries = 0;
    }
    else
    {
        memcpy(&dir->entries, &dirData[0], sizeof(DirEntryCount));
    }
    dir->childs = new Dirent[dir->entries];
    dir->pos = 0;

    FileSize p = sizeof(DirEntryCount);
    DirEntryCount entry = 0;
    while (p < dirPinode->size)
    {
        DirEntryLength direntryl = dirData[p];
        FileNamePos dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
        if (dirnamel > dirPinode->size - p)
        {
            // We have an error while reading
            PAFFS_DBG(PAFFS_TRACE_BUG, "Dirname length was bigger than possible "
                    "(%" PTYPE_FILNAMEPOS " > %" PTYPE_FILNAMEPOS ")!",
                    dirnamel, dirPinode->size - p);
            delete[] dir->childs;
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
        entry++;
    }

    if (entry != dir->entries)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Directory stated it had %" PTYPE_DIRENTRYCOUNT " entries, but has actually %" PTYPE_DIRENTRYCOUNT "!",
                  dir->entries,
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
    {
        return Result::notMounted;
    }
    if (dir->childs == nullptr)
    {
        return Result::invalidInput;
    }

    for (FileSize i = 0; i < dir->entries; i++)
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
    if (dir.entries == 0)
    {
        return nullptr;
    }

    if (dir.pos == dir.entries)
    {
        return nullptr;
    }

    if (dir.pos > dir.entries)
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
        lasterr = r;
        return nullptr;
    }
    if ((dir.childs[dir.pos].node->perm & R) == 0)
    {
        lasterr = Result::noPerm;
        dir.pos++;
        return nullptr;
    }

    if (dir.childs[dir.pos].node->type == InodeType::dir)
    {
        FileNamePos namel = strlen(dir.childs[dir.pos].name);
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
    {
        return Result::notMounted;
    }
    if (readOnly)
    {
        return Result::readOnly;
    }
    if (superblock.getUsedAreas() > areasNo - minFreeAreas)
    {
        return Result::noSpace;
    }

    FileNamePos lastSlash = 0;

    SmartInodePtr parDir;
    Result r = getParentDir(fullPath, parDir, &lastSlash);
    if (r != Result::ok)
    {
        return r;
    }

    if (strlen(&fullPath[lastSlash]) > maxDirEntryLength)
    {
        return Result::objNameTooLong;
    }

    if ((r = createFilInode(outFile, mask)) != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not create fileInode: %s", err_msg(r));
        return r;
    }

    journal.addEvent(journalEntry::device::MkObjInode(outFile->no));
    r = tree.insertInode(*outFile);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert Inode into tree: %s", err_msg(r));
        return r;
    }
    //Journal log is included in insertInodeInDir
    r = insertInodeInDir(&fullPath[lastSlash], *parDir, *outFile);
    if(r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new fil inode in parent dir");
        return r;
    }
    return Result::ok;
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
        lasterr = Result::readOnly;
        return nullptr;
    }
    SmartInodePtr file;
    Result r;
    r = getInodeOfElem(file, path);
    if (r == Result::notFound)
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
            lasterr = Result::notFound;
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
        lasterr = Result::invalidInput;
        return nullptr;
    }

    if ((file->perm | (mask & permMask)) != (file->perm & permMask))
    {
        lasterr = Result::noPerm;
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
    {
        return Result::notMounted;
    }
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
    {
        return Result::notMounted;
    }
    if (readOnly)
    {
        return Result::readOnly;
    }
    if (superblock.getUsedAreas() > areasNo - minFreeAreas)
    {   // If we use reserved Areas, extensive touching may fill flash
        return Result::noSpace;
    }

    SmartInodePtr file;
    Result r = getInodeOfElem(file, path);
    if (r == Result::notFound)
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

        return r;
    }
}

Result
Device::getObjInfo(const char* fullPath, ObjInfo& nfo)
{
    if (!mounted)
    {
        return Result::notMounted;
    }
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
Device::read(Obj& obj, void* buf, FileSize bytesToRead, FileSize* bytesRead)
{
    if (!mounted)
    {
        return Result::notMounted;
    }
    if (obj.dirent.node->type == InodeType::dir)
    {
        return lasterr = Result::invalidInput;
    }
    if (obj.dirent.node->type == InodeType::lnk)
    {
        return lasterr = Result::nimpl;
    }
    if ((obj.dirent.node->perm & R) == 0)
        return Result::noPerm;

    if (obj.dirent.node->size == 0)
    {
        *bytesRead = 0;
        return Result::ok;
    }

    Result r = dataIO.readInodeData(*obj.dirent.node, obj.fp, bytesToRead,
                                    bytesRead, static_cast<uint8_t*>(buf));
    if (r != Result::ok)
    {
        return r;
    }

    //*bytes_read = bytes_to_read;
    obj.fp += *bytesRead;
    return Result::ok;
}

Result
Device::write(Obj& obj, const void* buf, FileSize bytesToWrite,
              FileSize* bytesWritten)
{
    *bytesWritten = 0;
    if (!mounted)
    {
        return Result::notMounted;
    }
    if (readOnly)
    {
        return Result::readOnly;
    }
    if (superblock.getUsedAreas() > areasNo - minFreeAreas)
    {
        return Result::noSpace;
    }
    if (obj.dirent.node == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Objects dirent.node is invalid!");
        return Result::bug;
    }

    if (obj.dirent.node->type == InodeType::dir)
    {
        return Result::invalidInput;
    }
    if (obj.dirent.node->type == InodeType::lnk)
    {
        return Result::nimpl;
    }
    if ((obj.dirent.node->perm & W) == 0)
    {
        return Result::noPerm;
    }

    //TODO: Is inconsistent between write and update filsize.
    //TODO: Maybe include in dataIO!

    Result r = dataIO.writeInodeData(*obj.dirent.node, obj.fp,
                                     bytesToWrite, bytesWritten, static_cast<const uint8_t*>(buf));
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not write %" PTYPE_FILSIZE " of %" PTYPE_FILSIZE " bytes at %" PTYPE_FILSIZE "",
                  bytesToWrite - *bytesWritten,
                  bytesToWrite, obj.fp);
        if (*bytesWritten > 0)
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
    if (*bytesWritten != bytesToWrite)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not write Inode Data in whole! (Should: %" PTYPE_FILSIZE ", was: %" PTYPE_FILSIZE ")",
                  bytesToWrite,
                  *bytesWritten);
        // TODO: Handle error, maybe rewrite
        return Result::fail;
    }

    obj.dirent.node->mod =
            systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();

    obj.fp += *bytesWritten;
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

    journal.addEvent(journalEntry::Checkpoint(JournalEntry::Topic::dataIO));
    r = journal.addEvent(journalEntry::Checkpoint(getTopic()));
    if(r == Result::lowMem)
    {
        PAFFS_DBG(PAFFS_TRACE_DEVICE, "Journal nearly full, flushing caches");
        return flushAllCaches();
    }
    return Result::ok;
}

Result
Device::seek(Obj& obj, FileSizeDiff m, Seekmode mode)
{
    if (!mounted)
    {
        return Result::notMounted;
    }
    switch (mode)
    {
    case Seekmode::set:
        if (m < 0)
            return lasterr = Result::invalidInput;
        obj.fp = m;
        break;
    case Seekmode::end:
        if (-m > obj.dirent.node->size)
            return Result::invalidInput;
        obj.fp = obj.dirent.node->size + m;
        break;
    case Seekmode::cur:
        if (obj.fp + m < 0)
            return Result::invalidInput;
        obj.fp += m;
        break;
    }

    return Result::ok;
}

Result
Device::flush(Obj& obj)
{
    if (!mounted)
    {
        return Result::notMounted;
    }
    if (readOnly)
    {
        return Result::ok;
    }

    // TODO: When Inodes get Link to its PAC, this would be more elegant
    Result r = dataIO.pac.setTargetInode(*obj.dirent.node);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Target Inode!");
        return r;
    }
    r = dataIO.pac.commit();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Inode!");
        return r;
    }
    if(r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new Dir inode in parent dir");
        return r;
    }
    r = journal.addEvent(journalEntry::Checkpoint(getTopic()));
    if(r == Result::lowMem)
    {
        PAFFS_DBG(PAFFS_TRACE_DEVICE, "Journal nearly full, flushing caches");
        return flushAllCaches();
    }
    return Result::ok;
}

Result
Device::truncate(const char* path, FileSize newLength, bool fromUserspace)
{
    if (!mounted)
    {
        return Result::notMounted;
    }
    if (readOnly)
    {
        return Result::readOnly;
    }

    SmartInodePtr object;
    Result r;
    if ((r = getInodeOfElem(object, path)) != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not get Inode of elem");
        return r;
    }

    if (!(object->perm & W))
    {
        return Result::noPerm;
    }

    if (object->type == InodeType::dir)
    {
        if (object->size > sizeof(DirEntryCount))
        {
            return Result::dirNotEmpty;
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

    if(fromUserspace)
    {
        r = journal.addEvent(journalEntry::Checkpoint(getTopic()));
        if(r == Result::lowMem)
        {
            PAFFS_DBG(PAFFS_TRACE_DEVICE, "Journal nearly full, flushing caches");
            return flushAllCaches();
        }

    }

    return Result::ok;
}

Result
Device::remove(const char* path)
{
    SmartInodePtr object;
    Result r;

    if ((r = getInodeOfElem(object, path)) != Result::ok)
    {
        return r;
    }

    // If reference count is bigger than our own reference
    if (inodePool.map[object->no].second > 1)
    {
        // Still opened by others
        return Result::invalidInput;
    }

    SmartInodePtr parentDir;
    uint16_t lastSlash = 0;
    if ((r = getParentDir(path, parentDir, &lastSlash)) != Result::ok)
    {
        return r;
    }

    journal.addEvent(journalEntry::device::RemoveObj(object->no, parentDir->no));

    r = truncate(path, 0, false);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete the files contents");
        return r;
    }

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
    r = journal.addEvent(journalEntry::Checkpoint(getTopic()));
    if(r == Result::lowMem)
    {
        PAFFS_DBG(PAFFS_TRACE_DEVICE, "Journal nearly full, flushing caches");
        return flushAllCaches();
    }
    return Result::ok;
}

Result
Device::chmod(const char* path, Permission perm)
{
    if (!mounted)
    {
        return Result::notMounted;
    }
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

    return r;
}

Result
Device::getListOfOpenFiles(Obj* list[])
{
    uint16_t pos = 0;
    for (uint16_t i = 0; i < maxNumberOfFiles; i++)
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

JournalEntry::Topic
Device::getTopic()
{
    return JournalEntry::Topic::device;
}
void
Device::resetState()
{
    journalState = JournalState::ok;
}
Result
Device::processEntry(const journalEntry::Max& entry, JournalEntryPosition)
{
    //remember, all of these log messages have not gotten to checkpoint
    if(entry.base.topic == JournalEntry::Topic::device)
    {
        switch(entry.device.action)
        {
        case journalEntry::Device::Action::mkObjInode:
        {
            targetInodeNo = entry.device_.mkObjInode.inode;
            journalState = JournalState::makeObj;
            return Result::ok;
        }
        case journalEntry::Device::Action::insertIntoDir:
        {
            folderInodeNo = entry.device_.insertIntoDir.dirInode;
            journalState = JournalState::insertObj;
            return Result::ok;
        }
        case journalEntry::Device::Action::removeObj:
            targetInodeNo = entry.device_.removeObj.obj;
            folderInodeNo = entry.device_.removeObj.parDir;
            journalState = JournalState::removeObj;
        }
        return Result::ok;
    }
    return Result::bug;
}

void
Device::signalEndOfLog()
{
    Inode obj;
    Inode folder;
    Result r;

    //fixme Debug
    printf("device cleanup\n");
    debugPrintStatus();

    switch(journalState)
    {
    case JournalState::ok:
        //Step 1: be finished. Step 2: Hey, you're finished!
        return;
    case JournalState::makeObj:
        //we never got to insert it into directory
        tree.deleteInode(targetInodeNo);
        return;
    case JournalState::insertObj:
        //Maybe we inserted Obj into dir
        //We dont need the real Obj for that. It may have been deleted by aborted JournalRecover.
        r = tree.getInode(folderInodeNo, folder);
        if(r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Serious business, the folder is missing?");
            return;
        }
        {
            uint8_t namelength = 200;
            char name[200];
            r = getNameOfInodeInDir(targetInodeNo, folder, name, namelength);
            if(r == Result::notFound)
            {   //usually, this will happen
                tree.deleteInode(targetInodeNo);
                return;
            }
            PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Could restore file '%.*s', "
                    "\n\twow such journal many log, much restore", namelength, name);
            //to supress deletion of node in endOfLog
            return;
        }
    case JournalState::removeObj:
        r = tree.getInode(targetInodeNo, obj);
        if(r == Result::ok)
        {   //First, delete all contents of file
            if(obj.size != 0)
            {
                r = dataIO.deleteInodeData(obj, 0, true);
                if (r != Result::ok)
                {
                    PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete Inode Data");
                    return;
                }
                r = dataIO.pac.commit();
                if (r != Result::ok)
                {
                    PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit PAC");
                }
            }
            tree.deleteInode(targetInodeNo);
        }
        r = tree.getInode(folderInodeNo, folder);
        if(r != Result::ok)
        {   //serious business
            PAFFS_DBG(PAFFS_TRACE_BUG, "Could not get parent inode %" PTYPE_INODENO,
                      folderInodeNo);
            return;
        }
        removeInodeFromDir(folder, targetInodeNo);
        return;
    }
}

Result
Device::initializeDevice()
{
    if (mounted)
    {
        return Result::alrMounted;
    }
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Device is not yet mounted");

    superblock.clear();

    if (areasNo < AreaType::no - 2)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Device too small, at least %" PRIu8 " Areas are needed!",
                  AreaType::no);
        return Result::invalidInput;
    }

    if (blocksPerArea < 2)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Device too small, at least 2 Blocks per Area are needed!");
        return Result::invalidInput;
    }

    if (dataPagesPerArea > dataBytesPerPage * 8)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Device Areas too big, Area Summary would not fit a single page!");
        return Result::invalidInput;
    }

    if (blocksTotal % blocksPerArea != 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "'blocksPerArea' does not divide "
                  "%" PTYPE_BLOCKABS " blocks evenly! (define %" PTYPE_BLOCKABS ", rest: %" PTYPE_BLOCKABS ")",
                  blocksTotal,
                  blocksPerArea,
                  blocksTotal % blocksPerArea);
        return Result::invalidInput;
    }
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Init success");
    return Result::ok;
}

Result
Device::destroyDevice()
{
    journalState = JournalState::ok;
    superblock.clear();
    inodePool.clear();
    filesPool.clear();
    dataIO.pac.clear();
    sumCache.clear();
    return Result::ok;
}
};
