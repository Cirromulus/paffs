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

#ifndef PAFFS_FLASH_H_
#define PAFFS_FLASH_H_

#include "btree.hpp"
#include "commonTypes.hpp"
#include "pageAddressCache.hpp"
#include "superblock.hpp"

namespace paffs
{
class DataIO
{
    Device* dev;

    PageStateMachine<maxPagesPerWrite, JournalEntry::Topic::dataIO> statemachine;

public:
    PageAddressCache pac;

    DataIO(Device* mdev);
    // Updates changes to treeCache as well
    Result
    writeInodeData(Inode& inode,
                   FileSize offs,
                   FileSize bytes,
                   FileSize* bytesWritten,
                   const uint8_t* data);
    Result
    readInodeData(Inode& inode,
                  FileSize offs,
                  FileSize bytes,
                  FileSize* bytesRead,
                  uint8_t* data);
    Result
    deleteInodeData(Inode& inode, unsigned int offs);

private:
    /**
     * @param reservedPages Is increased if new page was used
     */
    Result
    writePageData(PageAbs  pageFrom,
                  PageAbs  pageTo,
                  FileSize offs,
                  FileSize bytes,
                  const uint8_t* data,
                  PageAddressCache& ac,
                  FileSize* bytes_written,
                  FileSize filesize,
                  uint32_t& reservedPages);
    Result
    readPageData(PageAbs  pageFrom,
                 PageAbs  pageTo,
                 FileSize offs,
                 FileSize bytes,
                 uint8_t* data,
                 PageAddressCache& ac,
                 FileSize* bytes_read);

    bool checkIfSaneReadAddress(Addr pageAddr);
};
};

#endif /* PAFFS_FLASH_H_ */
