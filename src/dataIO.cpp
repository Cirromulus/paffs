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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

namespace paffs
{
// modifies inode->size and inode->reserved size as well
Result
DataIO::writeInodeData(Inode& inode,
                       unsigned int offs,
                       unsigned int bytes,
                       unsigned int* bytes_written,
                       const char* data)
{
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried writing InodeData in readOnly mode!");
        return Result::bug;
    }
    if (offs + bytes == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Write size 0! Bug?");
        return Result::einval;
    }

    // todo: use pageFrom as offset to reduce memory usage and IO
    unsigned int pageFrom = offs / dataBytesPerPage;
    unsigned int toPage = (offs + bytes) / dataBytesPerPage;
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

    if (pageFrom > inode.size / dataBytesPerPage)
    {
        // We are skipping unused pages
        PageOffs lastUsedPage = inode.size / dataBytesPerPage;
        if (inode.size % dataBytesPerPage > 0)
        {
            lastUsedPage++;
        }

        for (unsigned i = lastUsedPage; i < pageFrom; i++)
        {
            pac.setPage(i, combineAddress(0, unusedMarker));
        }
    }

    res = writePageData(pageFrom,
                        toPage,
                        offs % dataBytesPerPage,
                        bytes,
                        data,
                        pac,
                        bytes_written,
                        inode.size,
                        inode.reservedPages);

    if (inode.size < *bytes_written + offs)
    {
        inode.size = *bytes_written + offs;
    }

    // the Tree UpdateExistingInode has to be done by high level functions,
    // bc they may modify it by themselves
    return res;
}

Result
DataIO::readInodeData(
        Inode& inode, unsigned int offs, unsigned int bytes, unsigned int* bytes_read, char* data)
{
    if (offs + bytes == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Read size 0! Bug?");
        return dev->lasterr = Result::einval;
    }

    if (offs + bytes > inode.size)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Read bigger than size of object! (was: %d, max: %lu)",
                  offs + bytes,
                  static_cast<long unsigned>(inode.size));
        bytes = inode.size - offs;
    }

    *bytes_read = 0;
    unsigned int pageFrom = offs / dataBytesPerPage;
    unsigned int toPage = (offs + bytes) / dataBytesPerPage;
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

    return readPageData(pageFrom, toPage, offs % dataBytesPerPage, bytes, data, pac, bytes_read);
}

// inode->size and inode->reservedSize is altered.
Result
DataIO::deleteInodeData(Inode& inode, unsigned int offs)
{
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried deleting InodeData in readOnly mode!");
        return Result::bug;
    }

    unsigned int pageFrom = offs / dataBytesPerPage;
    unsigned int toPage = inode.size / dataBytesPerPage;
    if (offs % dataBytesPerPage != 0)
    {
        pageFrom++;
    }
    if (inode.size % dataBytesPerPage == 0)
    {
        toPage--;
    }
    if (pageFrom > toPage)
    {
        // We are deleting just some bytes on the same page
        inode.size = offs;
        return Result::ok;
    }
    if (inode.reservedPages == 0)
    {
        inode.size = offs;
        return Result::ok;
    }

    if (inode.size < offs)
    {
        // Offset bigger than actual filesize
        return Result::einval;
    }

    Result r = pac.setTargetInode(inode);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "could not set new Inode!");
        return r;
    }

    for (int page = (toPage - pageFrom); page >= 0; page--)
    {
        Addr pageAddr;
        r = pac.getPage(page + pageFrom, &pageAddr);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Coud not get Page %u for read" PRIu32, page + pageFrom);
            return r;
        }
        unsigned int area = extractLogicalArea(pageAddr);
        unsigned int relPage = extractPageOffs(pageAddr);

        if (dev->areaMgmt.getType(area) != AreaType::data)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "DELETE INODE operation of invalid area at %d:%d",
                      extractLogicalArea(pageAddr),
                      extractPageOffs(pageAddr));
            return Result::bug;
        }

        if (dev->sumCache.getPageStatus(area, relPage, &r) == SummaryEntry::dirty)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "DELETE INODE operation of outdated (dirty)"
                      " data at %d:%d",
                      extractLogicalArea(pageAddr),
                      extractPageOffs(pageAddr));
            return Result::bug;
        }
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not load AreaSummary for area %d,"
                      " so no invalidation of data!",
                      area);
            return r;
        }

        // Mark old pages dirty
        r = dev->sumCache.setPageStatus(area, relPage, SummaryEntry::dirty);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not write AreaSummary for area %d,"
                      " so no invalidation of data!",
                      area);
            return r;
        }

        r = pac.setPage(page + pageFrom, 0);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete page %u to %u", pageFrom, toPage);
            return r;
        }

        inode.reservedPages--;
    }

    inode.size = offs;
    return Result::ok;
}

Result
DataIO::writePageData(PageOffs pageFrom,
                      PageOffs toPage,
                      unsigned offs,
                      unsigned bytes,
                      const char* data,
                      PageAddressCache& ac,
                      unsigned* bytes_written,
                      FileSize filesize,
                      uint32_t& reservedPages)
{
    // Will be set to zero after offset is applied
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried writing something in readOnly mode!");
        return Result::bug;
    }
    else if (offs > dataBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried applying an offset %d > %d", offs, dataBytesPerPage);
        return Result::bug;
    }
    *bytes_written = 0;
    Result res;
    for (unsigned int page = 0; page <= toPage - pageFrom; page++)
    {
        Result rBuf = dev->lasterr;
        dev->lasterr = Result::ok;
        dev->areaMgmt.findWritableArea(AreaType::data);
        if (dev->lasterr != Result::ok)
        {
            // TODO: Return to a safe state by trying to resurrect dirty marked pages
            //		Mark fresh written pages as dirty. If old pages have been deleted,
            //		use the Journal to resurrect (not currently implemented)
            return dev->lasterr;
        }
        dev->lasterr = rBuf;

        // Handle Areas
        if (dev->areaMgmt.getStatus(dev->areaMgmt.getActiveArea(AreaType::data))
            != AreaStatus::active)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned inactive area!");
            return Result::bug;
        }

        // find new page to write to
        unsigned int firstFreePage = 0;
        if (dev->areaMgmt.findFirstFreePage(&firstFreePage,
                                            dev->areaMgmt.getActiveArea(AreaType::data))
            == Result::nospace)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "BUG: findWritableArea returned full area (%d).",
                      dev->areaMgmt.getActiveArea(AreaType::data));
            return Result::bug;
        }
        Addr pageAddress =
                combineAddress(dev->areaMgmt.getActiveArea(AreaType::data), firstFreePage);

        Addr pageAddr;
        //GetPage may overwrite our driver buffer
        res = ac.getPage(page + pageFrom, &pageAddr);
        if (res != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Coud not get Page %u for write-back" PRIu32,
                      page + pageFrom);
            return res;
        }

        // Prepare buffer and calculate bytes to write
        unsigned int btw = bytes - *bytes_written;
        if ((btw + offs) > dataBytesPerPage)
        {
            btw = dataBytesPerPage - offs;
        }

        char* buf = dev->driver.getPageBuffer();

        // start misaligned || End Misaligned
        if (offs > 0 ||
           (btw + offs < dataBytesPerPage && page * dataBytesPerPage + btw < filesize))
        {
            // we are misaligned, so fill write buffer with valid Data
            unsigned int btr = dataBytesPerPage;

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

            if (pageAddr != 0  // not an empty page TODO: doubled code
                && pageAddr != combineAddress(0, unusedMarker))
            {  // not a skipped page (thus containing no information)
                // We are overriding real data, not just empty space
                unsigned int bytes_read = 0;
                Result r = readPageData(
                        pageFrom + page, pageFrom + page, 0, btr, buf, ac, &bytes_read);
                if (r != Result::ok || bytes_read != btr)
                {
                    return Result::bug;
                }
            }else
            {
                //We are overriding into nonexistent page, assume zero
                memset(buf, 0, dataBytesPerPage);
            }

            // Handle offset
            memcpy(&buf[offs], &data[*bytes_written], btw);

            // this is here, because btw will be modified
            *bytes_written += btw;

            // increase btw to whole page to write existing data back
            btw = btr > (offs + btw) ? btr : offs + btw;

            // offset is only applied to first page
            offs = 0;
        }
        else
        {
            // not misaligned, we are writing a whole page or a new page
            memcpy(buf, &data[*bytes_written], btw);
            *bytes_written += btw;
        }

        res = dev->driver.writePage(getPageNumber(pageAddress, *dev), buf, btw);
        if (res != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "ERR: write returned FAIL at phy.P: %llu",
                      static_cast<long long unsigned int>(getPageNumber(pageAddress, *dev)));
            return res;
        }
        res = dev->sumCache.setPageStatus(
                dev->areaMgmt.getActiveArea(AreaType::data), firstFreePage, SummaryEntry::used);
        if (res != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!",
                      resultMsg[static_cast<int>(res)]);
        }
        ac.setPage(page + pageFrom, pageAddress);

        // if we have overwriting existing data...
        if (pageAddr != 0  // not an empty page
            && pageAddr != combineAddress(0, unusedMarker))
        {  // not a skipped page (thus containing no information)
            // Mark old pages dirty
            // mark old Page in Areamap
            unsigned long oldArea = extractLogicalArea(pageAddr);
            unsigned long oldPage = extractPageOffs(pageAddr);

            res = dev->sumCache.setPageStatus(oldArea, oldPage, SummaryEntry::dirty);
            if (res != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!",
                          resultMsg[static_cast<int>(res)]);
                PAFFS_DBG_S(PAFFS_TRACE_WRITE,
                            "At pagelistindex %" PRIu32 ", oldArea: %lu, oldPage: %lu",
                            page + pageFrom,
                            oldArea,
                            oldPage);
            }
        }
        else
        {
            // or we added a new page to this file
            reservedPages++;
        }

        // this may have filled the flash
        res = dev->areaMgmt.manageActiveAreaFull(AreaType::data);
        if (res != Result::ok)
            return res;

        PAFFS_DBG_S(PAFFS_TRACE_WRITE,
                    "write r.P: %d/%d, phy.P: %llu",
                    page + 1,
                    toPage + 1,
                    static_cast<long long unsigned int>(getPageNumber(pageAddress, *dev)));
    }
    return Result::ok;
}

Result
DataIO::readPageData(PageOffs pageFrom,
                     PageOffs toPage,
                     unsigned offs,
                     unsigned bytes,
                     char* data,
                     PageAddressCache& ac,
                     unsigned* bytes_read)
{
    for (unsigned int page = 0; page <= toPage - pageFrom; page++)
    {
        Addr pageAddr;
        Result r = ac.getPage(page + pageFrom, &pageAddr);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Coud not get Page %u for read" PRIu32, page + pageFrom);
            return r;
        }

        unsigned int btr = bytes + offs - *bytes_read;
        if (btr > dataBytesPerPage)
        {
            btr = (bytes + offs) > (page + 1) * dataBytesPerPage
                          ? dataBytesPerPage
                          : (bytes + offs) - page * dataBytesPerPage;
        }

        if (pageAddr == combineAddress(0, unusedMarker))
        {
            // This Page is currently not written to flash
            // because it contains just empty space
            memset(&data[*bytes_read], 0, btr - offs);
            *bytes_read += btr - offs;
            offs = 0;   //Offset is only applied to first page
            continue;
        }


        if(!checkIfSaneReadAddress(pageAddr))
        {
            return Result::bug;
        }

        PageAbs addr = getPageNumber(pageAddr, *dev);
        char* buf = dev->driver.getPageBuffer();
        r = dev->driver.readPage(addr, buf, btr);
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
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read page, aborting pageData Read");
                return dev->lasterr = r;
            }
        }
        memcpy(&data[*bytes_read], &buf[offs], btr - offs);
        *bytes_read += btr - offs;
        offs = 0;   //offset is only applied to first page
    }

    return Result::ok;
}


bool DataIO::checkIfSaneReadAddress(Addr pageAddr)
{
    if (dev->areaMgmt.getType(extractLogicalArea(pageAddr)) != AreaType::data)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "READ INODE operation of invalid area at %" PRIu32 ":%" PRIu32,
                  extractLogicalArea(pageAddr),
                  extractPageOffs(pageAddr));
        return false;
    }

    if (traceMask & PAFFS_TRACE_VERIFY_AS)
    {
        Result r;
        SummaryEntry e = dev->sumCache.getPageStatus(
                extractLogicalArea(pageAddr), extractPageOffs(pageAddr), &r);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not load AreaSummary of area %d for verification!",
                      extractLogicalArea(pageAddr));
            return false;
        }
        else
        {
            if (e == SummaryEntry::dirty)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "READ INODE operation of outdated (dirty) data at %d:%d",
                          extractLogicalArea(pageAddr),
                          extractPageOffs(pageAddr));
                return false;
            }

            if (e == SummaryEntry::free)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "READ INODE operation of invalid (free) data at %d:%d",
                          extractLogicalArea(pageAddr),
                          extractPageOffs(pageAddr));
                return false;
            }

            if (e >= SummaryEntry::error)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "READ INODE operation of data with invalid AreaSummary at area %d!",
                          extractLogicalArea(pageAddr));
                return false;
            }
        }
    }
    return true;
}
}
