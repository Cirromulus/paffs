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

public:
    PageAddressCache pac;

    DataIO(Device* mdev) : dev(mdev), pac(*mdev){};
    // Updates changes to treeCache as well
    Result
    writeInodeData(Inode& inode,
                   unsigned int offs,
                   unsigned int bytes,
                   unsigned int* bytesWritten,
                   const char* data);
    Result
    readInodeData(Inode& inode,
                  unsigned int offs,
                  unsigned int bytes,
                  unsigned int* bytesRead,
                  char* data);
    Result
    deleteInodeData(Inode& inode, unsigned int offs);

private:
    /**
     * @param reservedPages Is increased if new page was used
     */
    Result
    writePageData(PageOffs pageFrom,
                  PageOffs pageTo,
                  unsigned offs,
                  unsigned bytes,
                  const char* data,
                  PageAddressCache& ac,
                  unsigned* bytes_written,
                  FileSize filesize,
                  uint32_t& reservedPages);
    Result
    readPageData(PageOffs pageFrom,
                 PageOffs pageTo,
                 unsigned offs,
                 unsigned bytes,
                 char* data,
                 PageAddressCache& ac,
                 unsigned* bytes_read);

    bool checkIfSaneReadAddress(Addr pageAddr);
};
};

#endif /* PAFFS_FLASH_H_ */
