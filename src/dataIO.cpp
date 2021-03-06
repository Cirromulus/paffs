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

#include "dataIO.hpp"
#include "area.hpp"
#include "device.hpp"
#include "driver/driver.hpp"
#include "paffs_trace.hpp"
#include "journalPageStatemachine_impl.hpp"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

namespace paffs
{

DataIO::DataIO(Device *mdev) : dev(mdev), pac(*mdev), statemachine(mdev->journal, mdev->sumCache, &pac)
{
    resetState();
};

// modifies inode->size and inode->reserved size as well
Result
DataIO::writeInodeData(Inode& inode,
                       FileSize offs,
                       FileSize bytes,
                       FileSize* bytesWritten,
                       const uint8_t* data)
{
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried writing InodeData in readOnly mode!");
        return Result::bug;
    }
    if (offs + bytes == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Write size 0! Bug?");
        return Result::invalidInput;
    }
    // todo: use pageFrom as offset to reduce memory usage and IO
    FileSize pageFrom = offs / dataBytesPerPage;
    FileSize toPage = (offs + bytes) / dataBytesPerPage;
    if ((offs + bytes) % dataBytesPerPage == 0)
    {
        toPage--;
    }

    if(toPage - pageFrom > maxPagesPerWrite)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Due to the journal, only %" PRIu16 " pages "
                "are allowed per single write", maxPagesPerWrite);
        return Result::tooBig;
    }

    Result res = pac.setTargetInode(inode);
    if (res != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "could not set new Inode!");
        return res;
    }

    if (pageFrom > inode.size / dataBytesPerPage)
    {
        // We are skipping unused pages
        PageOffs lastUsedPage = inode.size / dataBytesPerPage;
        if (inode.size % dataBytesPerPage > 0)
        {
            lastUsedPage++;
        }
    }

    FAILPOINT;
    if (inode.size < bytes + offs)
    {   //this will only be applied if write succeeds
        dev->journal.addEvent(journalEntry::dataIO::NewInodeSize(inode.no, bytes + offs));
    }
    FAILPOINT;
    res = writePageData(pageFrom,
                        toPage,
                        offs % dataBytesPerPage,
                        bytes,
                        data,
                        pac,
                        bytesWritten,
                        inode.size,
                        inode.reservedPages);
    FAILPOINT;
    if(res != Result::ok)
    {
        //TODO: revert Statemachine
        return res;
    }
    if (inode.size < *bytesWritten + offs)
    {
        inode.size = *bytesWritten + offs;
    }
    inode.mod = systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();

	//This is the success message for dataIO and pageAddressCache
	res = dev->tree.updateExistingInode(inode);
	if(res != Result::ok)
	{
		//TODO: revert Statemachine
		return res;
	}
	FAILPOINT;
    res = statemachine.invalidateOldPages();
    if (res != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!",
                  resultMsg[static_cast<int>(res)]);
    }
	
    pac.setValid();

    //Checkpoint is done by device functions, because a write-truncate pair has to be kept together
    return res;
}

Result
DataIO::readInodeData(Inode& inode,
                      FileSize offs,
                      FileSize bytes,
                      FileSize* bytesRead,
                      uint8_t* data)
{
    if (offs + bytes == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Read size 0! Bug?");
        return dev->lasterr = Result::invalidInput;
    }

    if (offs + bytes > inode.size)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Read bigger than size of object! (was: %" PRIu32 ", max: %" PRIu32 ")",
                  offs + bytes,
                  inode.size);
        bytes = inode.size < offs ? 0 : inode.size - offs;
    }

    *bytesRead = 0;
    PageAbs pageFrom = offs / dataBytesPerPage;
    PageAbs toPage = (offs + bytes) / dataBytesPerPage;
    if ((offs + bytes) % dataBytesPerPage == 0)
    {
        toPage--;
    }

    Result res = pac.setTargetInode(inode);
    if (res != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "could not set new Inode!");
        return res;
    }

    return readPageData(pageFrom, toPage, offs % dataBytesPerPage, bytes, data, pac, bytesRead);
}

// inode->size and inode->reservedSize is altered.
Result
DataIO::deleteInodeData(Inode& inode, unsigned int offs, bool journalMode)
{
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried deleting InodeData in readOnly mode!");
        return Result::bug;
    }

    if(inode.size == offs)
    {   //nothing to do
        return Result::ok;
    }

    FileSize pageFrom = offs / dataBytesPerPage;
    FileSize toPage = inode.size / dataBytesPerPage;
    if (offs % dataBytesPerPage != 0)
    {
        pageFrom++;
    }
    if (inode.size % dataBytesPerPage == 0)
    {
        toPage--;
    }
    if (inode.size < offs)
    {
        // Offset bigger than actual filesize
        return Result::invalidInput;
    }

    Result r = pac.setTargetInode(inode);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "could not set new Inode!");
        return r;
    }
    FAILPOINT;
    dev->journal.addEvent(journalEntry::dataIO::NewInodeSize(inode.no, offs));
    FAILPOINT;
    if (pageFrom <= toPage && inode.reservedPages != 0)
    {
        //If we dont need one or more page anymore, mark them dirty
        for (int32_t page = (toPage - pageFrom); page >= 0; page--)
        {
            Addr pageAddr;
            r = pac.getPage(page + pageFrom, &pageAddr);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Coud not get Page %" PRId32 " for read", page + pageFrom);
                return r;
            }
            if(pageAddr == 0)
            {   //If we continue an aborted deletions
                continue;
            }

            AreaPos  area = extractLogicalArea(pageAddr);
            PageOffs relPage = extractPageOffs(pageAddr);

            if (dev->superblock.getType(area) != AreaType::data)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "DELETE INODE operation of invalid area at %" PRId16 ":%" PRId16 "",
                          extractLogicalArea(pageAddr),
                          extractPageOffs(pageAddr));
                return Result::bug;
            }
            FAILPOINT;
            if (!journalMode && dev->sumCache.getPageStatus(area, relPage, r) == SummaryEntry::dirty)
            {
                //In journalMode, it may happen that a page was already deleted
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "DELETE INODE operation of outdated (dirty)"
                          " data at %" PRId16 ":%" PRId16 "",
                          extractLogicalArea(pageAddr),
                          extractPageOffs(pageAddr));
                return Result::bug;
            }
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not load AreaSummary for area %" PRId16 ","
                          " so no invalidation of data!",
                          area);
                return r;
            }
            FAILPOINT;
            // Mark old pages dirty
            statemachine.replacePage(0, pageAddr, inode.no, page + pageFrom);

            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not write AreaSummary for area %" PRId16 ","
                          " so no invalidation of data!",
                          area);
                return r;
            }
            FAILPOINT;
            r = pac.setPage(page + pageFrom, 0);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete page %" PRIu32 " to %" PRIu32 "", pageFrom, toPage);
                return r;
            }
            FAILPOINT;
            inode.reservedPages--;
        }
    }
    inode.size = offs;
    dev->tree.updateExistingInode(inode);
    FAILPOINT;
    statemachine.invalidateOldPages();
    FAILPOINT;
    pac.setValid();
    FAILPOINT;
    dev->journal.addEvent(journalEntry::Checkpoint(JournalEntry::Topic::dataIO));
    FAILPOINT;
    return Result::ok;
}

JournalEntry::Topic
DataIO::getTopic()
{
    return JournalEntry::Topic::dataIO;
}

void
DataIO::resetState()
{
    statemachine.clear();
    memset(&journalLastModifiedInode, 0, sizeof(Inode));
    journalLastSize = 0;
    journalInodeValid = false;
    modifiedInode = false;
    processedForeignSuccessElement = false;
    journalIsWriteTruncatePair = false;
}

bool
DataIO::isInterestedIn(const journalEntry::Max& entry)
{
    return modifiedInode && !processedForeignSuccessElement &&
            entry.base.topic == JournalEntry::Topic::tree &&
            entry.btree.op == journalEntry::BTree::Operation::update;
}

Result
DataIO::processEntry(const journalEntry::Max& entry, JournalEntryPosition)
{
    if(entry.base.topic == getTopic())
    {
        switch(entry.dataIO.operation)
        {
        case journalEntry::DataIO::Operation::newInodeSize:
            if(journalInodeValid &&
               journalLastModifiedInode.no == entry.dataIO_.newInodeSize.inodeNo &&
               journalLastSize == entry.dataIO_.newInodeSize.filesize)
            {
                //If the same message comes again, we were truncating a directory (write-delete pair)
                //so we dont reset the "modified Inode" switch
                journalIsWriteTruncatePair = true;
                return Result::ok;
            }

            journalLastModifiedInode.no = entry.dataIO_.newInodeSize.inodeNo;
            journalLastSize = entry.dataIO_.newInodeSize.filesize;
            journalInodeValid = true;
            modifiedInode = false;
        }
    }
    else if(entry.base.topic == JournalEntry::Topic::pagestate)
    {
        modifiedInode = true;
        return statemachine.processEntry(entry);
    }
    else if(entry.base.topic == JournalEntry::Topic::tree)
    {
        if(entry.btree.op == journalEntry::BTree::Operation::update)
        {
            processedForeignSuccessElement = true;
            journalEntry::Max success;
            success.pagestate_.success = journalEntry::pagestate::Success(getTopic());
            return statemachine.processEntry(success);
        }
        return Result::ok;
    }
    else
    {
        return Result::bug;
    }
    return Result::ok;
}

void
DataIO::signalEndOfLog()
{
    JournalState state = statemachine.signalEndOfLog();
    if(journalInodeValid &&
            (state == JournalState::recover || //we continued action
            (state == JournalState::invalid && journalIsWriteTruncatePair) ||
            (state == JournalState::ok && processedForeignSuccessElement))) //We just did not have a checkpoint
    {
        Result r = dev->tree.getInode(journalLastModifiedInode.no, journalLastModifiedInode);
        if(r != Result::ok)
        {
            //It was already deleted, so ok
            return;
        }
        if(journalLastModifiedInode.size != journalLastSize)
        {
            PAFFS_DBG_S(PAFFS_TRACE_DEVICE | PAFFS_TRACE_JOURNAL,
                      "Recovered Write/deletion, changing Inode %" PTYPE_INODENO " size "
                      "from %" PTYPE_FILSIZE " to %" PTYPE_FILSIZE,
                      journalLastModifiedInode.no, journalLastModifiedInode.size, journalLastSize);
            if(journalLastModifiedInode.size < journalLastSize)
            {   //write
                if(modifiedInode)
                {
                    journalLastModifiedInode.size = journalLastSize;
                    dev->tree.updateExistingInode(journalLastModifiedInode);
                }
            }else
            {   //delete
                //FIXME Dont delete Inode data if we were merely writing.
                //This may be a bug saying 'newInodeSize', but Folder gets truncated before it was written.
                deleteInodeData(journalLastModifiedInode, journalLastSize, true);
            }
        }

    }
    //If an area was filled
    dev->areaMgmt.manageActiveAreaFull(AreaType::data);
    dev->areaMgmt.manageActiveAreaFull(AreaType::index);
    dev->journal.addEvent(journalEntry::Checkpoint(getTopic()));
}

Result
DataIO::writePageData(PageAbs  pageFrom,
                      PageAbs  toPage,
                      FileSize offs,
                      FileSize bytes,
                      const uint8_t* data,
                      PageAddressCache& ac,
                      FileSize* bytesWritten,
                      FileSize  filesize,
                      uint16_t& reservedPages)
{
    // Will be set to zero after offset is applied
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried writing something in readOnly mode!");
        return Result::bug;
    }else
    if (offs > dataBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried applying an offset %" PRIu32 " > %" PRIu16 "", offs, dataBytesPerPage);
        return Result::bug;
    }else
    if(toPage < pageFrom)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "From Page %" PRIu32 " > to page %" PRIu32 "!", pageFrom, toPage);
        return Result::bug;
    }
    *bytesWritten = 0;
    Result res;
    for (PageOffs page = 0; page <= static_cast<PageOffs>(toPage - pageFrom); page++)
    {
        Result rBuf = dev->lasterr;
        dev->lasterr = Result::ok;
        dev->areaMgmt.findWritableArea(AreaType::data);
        if (dev->lasterr != Result::ok)
        {
            // TODO: Return to a safe state by trying to resurrect dirty marked pages
            //		Mark fresh written pages as dirty. If old pages have been deleted,
            //		use the Journal to resurrect (not currently implemented)
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find writable area");
            dev->debugPrintStatus();
            return dev->lasterr;
        }
        dev->lasterr = rBuf;
        FAILPOINT;
        // Handle Areas
        if (dev->superblock.getStatus(dev->superblock.getActiveArea(AreaType::data))
            != AreaStatus::active)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned inactive area!");
            return Result::bug;
        }

        // find new page to write to
        PageOffs firstFreePage = 0;
        if (dev->areaMgmt.findFirstFreePage(firstFreePage,
                                            dev->superblock.getActiveArea(AreaType::data))
            == Result::noSpace)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "BUG: findWritableArea returned full area (%" PRId16 ").",
                      dev->superblock.getActiveArea(AreaType::data));
            return Result::bug;
        }
        Addr newAddress =
                combineAddress(dev->superblock.getActiveArea(AreaType::data), firstFreePage);
        FAILPOINT;
        Addr oldAddr;
        //GetPage may overwrite our driver buffer
        res = ac.getPage(page + pageFrom, &oldAddr);
        if (res != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Coud not get Page %" PRIu32 " for write-back" PRIu32,
                      page + pageFrom);
            return res;
        }

        // Prepare buffer and calculate bytes to write
        FileSize btw = bytes - *bytesWritten;
        if ((btw + offs) > dataBytesPerPage)
        {
            btw = dataBytesPerPage - offs;
        }
        FAILPOINT;
        //This has to be done before we write into the pagebuffer, this may modify it!
        res = statemachine.replacePage(newAddress, oldAddr, ac.getTargetInode(), page + pageFrom);
        if (res != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!",
                      resultMsg[static_cast<int>(res)]);
        }

        uint8_t* buf = dev->driver.getPageBuffer();

        // start misaligned || End Misaligned
        if (offs > 0 ||
           (btw + offs < dataBytesPerPage && page * dataBytesPerPage + btw < filesize))
        {
            // we are misaligned, so fill write buffer with valid Data
            uint16_t btr = dataBytesPerPage;

            // limit maximum bytes to read if file is smaller than actual page
            if ((pageFrom + 1 + page) * dataBytesPerPage > filesize)
            {
                if (filesize > (pageFrom + page) * dataBytesPerPage)
                {
                    btr = filesize - (pageFrom + page) * dataBytesPerPage;
                }
                else
                {
                    btr = 0;
                }
            }

            if (oldAddr != 0)  // not an empty page TODO: doubled code)
            {  // not a skipped page (thus containing no information)
                // We are overriding real data, not just empty space
                FileSize bytesRead = 0;
                Result r = readPageData(
                        pageFrom + page, pageFrom + page, 0, btr, buf, ac, &bytesRead);
                if (r != Result::ok || bytesRead != btr)
                {
                    return Result::bug;
                }
            }else
            {
                //We are overriding into nonexistent page, assume zero
                memset(buf, 0, dataBytesPerPage);
            }

            // Handle offset
            memcpy(&buf[offs], &data[*bytesWritten], btw);

            // this is here, because btw will be modified
            *bytesWritten += btw;

            // increase btw to whole page to write existing data back
            btw = btr > (offs + btw) ? btr : offs + btw;

            // offset is only applied to first page
            offs = 0;
        }
        else
        {
            // not misaligned, we are writing a whole page or a new page
            memcpy(buf, &data[*bytesWritten], btw);
            *bytesWritten += btw;
        }
        FAILPOINT;
        res = dev->driver.writePage(getPageNumber(newAddress, *dev), buf, btw);
        if (res != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "ERR: write returned FAIL at phy.P: %" PTYPE_PAGEABS,
                      getPageNumber(newAddress, *dev));
            //TODO: Revert all new Pages
            return res;
        }
        FAILPOINT;
        ac.setPage(page + pageFrom, newAddress);

        if (oldAddr == 0)
        {   //we added a new page to this file
            reservedPages++;
        }
        FAILPOINT;
        // this may have filled the flash
        res = dev->areaMgmt.manageActiveAreaFull(AreaType::data);
        if (res != Result::ok)
        {
            return res;
        }

        PAFFS_DBG_S(PAFFS_TRACE_WRITE,
                    "write r.P: %" PTYPE_PAGEOFFS "/%" PTYPE_PAGEABS ", "
                    "%" PTYPE_AREAPOS "(on %" PTYPE_AREAPOS "):%" PTYPE_PAGEOFFS,
                    page + 1,
                    toPage - 1,
                    extractLogicalArea(newAddress), dev->superblock.getPos(extractLogicalArea(newAddress)),
                    extractPageOffs(newAddress));
    }
    return Result::ok;
}

Result
DataIO::readPageData(PageAbs  pageFrom,
                     PageAbs  toPage,
                     FileSize offs,
                     FileSize bytes,
                     uint8_t* data,
                     PageAddressCache& ac,
                     FileSize* bytesRead)
{
    if(toPage < pageFrom)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "From Page %" PRIu32 " > to page %" PRIu32 "!", pageFrom, toPage);
        return Result::bug;
    }
    for (PageOffs page = 0; page <= static_cast<PageOffs>(toPage - pageFrom); page++)
    {
        Addr pageAddr;
        Result r = ac.getPage(page + pageFrom, &pageAddr);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Coud not get Page %" PRIu32 " for read" PRIu32, page + pageFrom);
            return r;
        }

        FileSize btr = bytes + offs - *bytesRead;
        if (btr > dataBytesPerPage)
        {
            btr = dataBytesPerPage;
        }

        if (pageAddr == 0)
        {
            // This Page is currently not written to flash
            // because it contains just empty space
            memset(&data[*bytesRead], 0, btr - offs);
            *bytesRead += btr - offs;
            offs = 0;   //Offset is only applied to first page
            continue;
        }

        if(!checkIfSaneReadAddress(pageAddr))
        {
            return Result::bug;
        }

        PageAbs physPage = getPageNumber(pageAddr, *dev);
        uint8_t* buf = dev->driver.getPageBuffer();
        r = dev->driver.readPage(physPage, buf, btr);
        if (r != Result::ok)
        {
            if (r == Result::biterrorCorrected)
            {
                // TODO rewrite page
                PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write "
                                              "corrected version back to flash.");
            }
            else
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read page at "
                        "%" PTYPE_AREAPOS "(on %" PTYPE_AREAPOS "):%" PTYPE_PAGEOFFS
                        ", aborting pageData Read",
                        extractLogicalArea(pageAddr), dev->superblock.getPos(extractLogicalArea(pageAddr)),
                        extractPageOffs(pageAddr));
                return dev->lasterr = r;
            }
        }
        memcpy(&data[*bytesRead], &buf[offs], btr - offs);
        *bytesRead += btr - offs;
        offs = 0;   //offset is only applied to first page
    }

    return Result::ok;
}


bool DataIO::checkIfSaneReadAddress(Addr pageAddr)
{
    if (dev->superblock.getType(extractLogicalArea(pageAddr)) != AreaType::data)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "READ INODE operation of invalid area (%s) at %" PTYPE_AREAPOS "(on %" PTYPE_AREAPOS "):%" PTYPE_PAGEOFFS,
                  areaNames[dev->superblock.getType(extractLogicalArea(pageAddr))],
                  extractLogicalArea(pageAddr),
                  dev->superblock.getPos(extractLogicalArea(pageAddr)),
                  extractPageOffs(pageAddr));
        return false;
    }

    if (traceMask & PAFFS_TRACE_VERIFY_AS)
    {
        Result r;
        SummaryEntry e = dev->sumCache.getPageStatus(pageAddr, r);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not load AreaSummary of area %" PTYPE_AREAPOS "(on %" PTYPE_AREAPOS ") for verification!",
                      extractLogicalArea(pageAddr),
                      dev->superblock.getPos(extractLogicalArea(pageAddr)));
            return false;
        }
        else
        {
            if (e == SummaryEntry::dirty)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "READ INODE operation of outdated (dirty) data at %" PTYPE_AREAPOS "(on %" PTYPE_AREAPOS "):%" PTYPE_PAGEOFFS "",
                          extractLogicalArea(pageAddr),
                          dev->superblock.getPos(extractLogicalArea(pageAddr)),
                          extractPageOffs(pageAddr));
                return false;
            }
            else
            if (e == SummaryEntry::free)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "READ INODE operation of invalid (free) data at %" PTYPE_AREAPOS "(on %" PTYPE_AREAPOS "):%" PTYPE_PAGEOFFS "",
                          extractLogicalArea(pageAddr),
                          dev->superblock.getPos(extractLogicalArea(pageAddr)),
                          extractPageOffs(pageAddr));
                return false;
            }
            else
            if (e >= SummaryEntry::error)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "READ INODE operation of data with invalid AreaSummary at area %" PTYPE_AREAPOS "(on %" PTYPE_AREAPOS ")!",
                          extractLogicalArea(pageAddr),
                          dev->superblock.getPos(extractLogicalArea(pageAddr)));
                return false;
            }
        }
    }
    return true;
}
}
