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

#include "area.hpp"
#include "btree.hpp"
#include "commonTypes.hpp"
#include "dataIO.hpp"
#include "driver/driver.hpp"
#include "journal.hpp"
#include "summaryCache.hpp"
#include "superblock.hpp"
#include <outpost/rtos/clock.h>
#include <outpost/time/clock.h>

#include "pools.hpp"

#pragma once

namespace paffs
{
extern outpost::rtos::SystemClock systemClock;

class Device : public JournalTopic
{
    InodePool<maxNumberOfInodes> inodePool;
    ObjectPool<Obj, maxNumberOfFiles> filesPool;
    bool useJournal = false;
public:
    Driver& driver;
    Result lasterr;
    bool mounted;
    bool readOnly;

    Btree tree;
    SummaryCache sumCache;
    AreaManagement areaMgmt;
    DataIO dataIO;
    Superblock superblock;
    MramPersistence journalPersistence;
    Journal journal;

    Device(Driver& driver);
    ~Device();

    /**
     * \param[in] badBlockList may be empty signalling no known bad blocks
     * \param[in] complete if true, delete complete flash (may take a while)
     * if false (default), only the superblocks are erased,
     * everything else is considered deleted
     * \warn Journal is disabled during format
     */
    Result
    format(const BadBlockList& badBlockList, bool complete = false);

    Result
    mnt(bool readOnlyMode = false);
    Result
    unmnt();

    // Directory
    Result
    mkDir(const char* fullPath, Permission mask);
    Dir*
    openDir(const char* path);
    Result
    closeDir(Dir*& dir);
    Dirent*
    readDir(Dir& dir);
    void
    rewindDir(Dir& dir);

    // File
    Obj*
    open(const char* path, Fileopenmask mask);
    Result
    close(Obj& obj);
    Result
    touch(const char* path);
    Result
    getObjInfo(const char* fullPath, ObjInfo& nfo);
    Result
    read(Obj& obj, void* buf, FileSize bytesToRead, FileSize* bytesRead);
    Result
    write(Obj& obj, const void* buf, FileSize bytesToWrite, FileSize* bytesWritten);
    Result
    seek(Obj& obj, FileSizeDiff m, Seekmode mode);
    Result
    flush(Obj& obj);
    Result
    truncate(const char* path, FileSize newLength);
    Result
    remove(const char* path);
    Result
    chmod(const char* path, Permission perm);
    Result
    getListOfOpenFiles(Obj* list[]);
    uint8_t
    getNumberOfOpenFiles();
    uint8_t
    getNumberOfOpenInodes();

    JournalEntry::Topic
    getTopic();
    Result
    processEntry(const journalEntry::Max& entry);
    void
    signalEndOfLog();

private:
    Result
    initializeDevice();
    Result
    destroyDevice();

    Result
    createInode(SmartInodePtr& outInode, Permission mask);
    Result
    createDirInode(SmartInodePtr& outInode, Permission mask);
    Result
    createFilInode(SmartInodePtr& outInode, Permission mask);
    Result
    getParentDir(const char* fullPath, SmartInodePtr& parDir, FileNamePos* lastSlash);
    Result
    getInodeNoInDir(InodeNo& outInode, Inode& folder, const char* name);
    /**
     * @param outInode has to point to an Inode, it is used as a buffer!
     */
    Result
    getInodeOfElem(SmartInodePtr& outInode, const char* fullPath);
    /**
     * @warn does not store the target into List, just checks whether it has to load it from tree
     * TODO: Future possibility is to store this target into cache (no buffer needed)
     *
     * @param target shall not point to valid data
     */
    Result
    findOrLoadInode(InodeNo no, SmartInodePtr& target);

    // newElem should be already inserted in Tree
    Result
    insertInodeInDir(const char* name, Inode& contDir, Inode& newElem);
    Result
    removeInodeFromDir(Inode& contDir, InodeNo elem);
    Result
    createFile(SmartInodePtr& outFile, const char* fullPath, Permission mask);
};

}  // namespace paffs
