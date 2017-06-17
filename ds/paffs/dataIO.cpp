/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: Pascal Pieper
 */

#include "dataIO.hpp"
#include "driver/driver.hpp"
#include "area.hpp"
#include "device.hpp"
#include "paffs_trace.hpp"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

namespace paffs{

//modifies inode->size and inode->reserved size as well
Result DataIO::writeInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_written,
					const char* data){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Write size 0! Bug?");
		return Result::einval;
	}

	//todo: use pageFrom as offset to reduce memory usage and IO
	unsigned int pageFrom = offs/dataBytesPerPage;
	unsigned int toPage = (offs + bytes - 1) / dataBytesPerPage;

	Result res;
	pac.setTargetInode(inode);

	unsigned pageoffs = offs % dataBytesPerPage;
	res = writePageData(pageFrom, toPage, pageoffs, bytes, data,
			pac, bytes_written, inode->size, inode->reservedPages);
	if(res != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "could not write pageData");
		return res;
	}

	if(inode->size < *bytes_written + offs)
		inode->size = *bytes_written + offs;

	return pac.commit();
}

Result DataIO::readInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read size 0! Bug?");
		return dev->lasterr = Result::einval;
	}

	*bytes_read = 0;
	unsigned int pageFrom = offs/dataBytesPerPage;
	unsigned int toPage = (offs + bytes - 1) / dataBytesPerPage;

	if(offs + bytes > inode->size){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read bigger than size of object! (was: %d, max: %lu)", offs+bytes, static_cast<long unsigned>(inode->size));
		bytes = inode->size - offs;
	}

	pac.setTargetInode(inode);

	return readPageData(pageFrom, toPage, offs % dataBytesPerPage, bytes, data, pac, bytes_read);
}

//inode->size and inode->reservedSize is altered.
Result DataIO::deleteInodeData(Inode* inode, unsigned int offs){
	//TODO: This calculation contains errors in border cases
	unsigned int pageFrom = offs/dataBytesPerPage;
	unsigned int toPage = (inode->size - 1) / dataBytesPerPage;

	if(inode->size < offs){
		//Offset bigger than actual filesize
		return Result::einval;
	}

	pac.setTargetInode(inode);

	inode->size = offs;

	if(inode->reservedPages == 0)
		return Result::ok;

	if(inode->size >= (inode->reservedPages - 1) * dataBytesPerPage)
		//doesn't leave a whole page blank
		return Result::ok;


	for(unsigned int page = 0; page <= toPage - pageFrom; page++){
		Addr pageAddr;
		Result r = pac.getPage(page+pageFrom, &pageAddr);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Coud not get Page %u for read" PRIu32, page+pageFrom);
			return r;
		}
		unsigned int area = extractLogicalArea(pageAddr);
		unsigned int relPage = extractPage(pageAddr);

		if(dev->areaMap[area].type != AreaType::data){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of invalid area at %d:%d",
					extractLogicalArea(pageAddr),
					extractPage(pageAddr));
			return Result::bug;
		}

		if(dev->sumCache.getPageStatus(area, relPage, &r) == SummaryEntry::dirty){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of outdated (dirty)"
					" data at %d:%d", extractLogicalArea(pageAddr),
					extractPage(pageAddr));
			return Result::bug;
		}
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load AreaSummary for area %d,"
					" so no invalidation of data!", area);
			return r;
		}

		//Mark old pages dirty
		r = dev->sumCache.setPageStatus(area, relPage, SummaryEntry::dirty);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write AreaSummary for area %d,"
					" so no invalidation of data!", area);
			return r;
		}

		inode->reservedPages--;
		r = pac.setPage(page+pageFrom, 0);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set page %u to zero", page+pageFrom);
			return r;
		}
	}

	return pac.commit();
}

//Does not change addresses in parent Nodes
Result DataIO::writeTreeNode(TreeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode NULL");
				return Result::bug;
	}
	if(sizeof(TreeNode) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page"
				" (Was %lu, should %u)", sizeof(TreeNode), dataBytesPerPage);
		return Result::bug;
	}

	dev->lasterr = Result::ok;
	dev->activeArea[AreaType::index] = dev->areaMgmt.findWritableArea(AreaType::index);
	if(dev->lasterr != Result::ok){
		return dev->lasterr;
	}
	if(dev->activeArea[AreaType::index] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
		return Result::bug;
	}

	if(node->self != 0){
		//We have to invalidate former position first
		Result r = dev->sumCache.setPageStatus(extractLogicalArea(node->self), extractPage(node->self), SummaryEntry::dirty);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not invalidate old Page! Ignoring Errors to continue...");
			//return r;
		}
	}

	unsigned int firstFreePage = 0;
	if(dev->areaMgmt.findFirstFreePage(&firstFreePage, dev->activeArea[AreaType::index]) == Result::nosp){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::index]);
		return dev->lasterr = Result::bug;
	}
	Addr addr = combineAddress(dev->activeArea[AreaType::index], firstFreePage);
	node->self = addr;

	//Mark Page as used
	Result r = dev->sumCache.setPageStatus(dev->activeArea[AreaType::index], firstFreePage, SummaryEntry::used);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not mark Page as used!");
		return r;
	}


	r = dev->driver->writePage(getPageNumber(node->self, dev), node, sizeof(TreeNode));
	if(r != Result::ok){
		//TODO: Revert Changes to PageStatus
		return r;
	}

	r = dev->areaMgmt.manageActiveAreaFull(&dev->activeArea[AreaType::index], AreaType::index);
	if(r != Result::ok)
		return r;

	return Result::ok;
}

Result DataIO::readTreeNode(Addr addr, TreeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode NULL");
		return Result::bug;
	}
	if(sizeof(TreeNode) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page (Was %lu, should %u)", sizeof(TreeNode), dataBytesPerPage);
		return Result::bug;
	}

	if(dev->areaMap[extractLogicalArea(addr)].type != AreaType::index){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREEENODE operation on %s!", areaNames[dev->areaMap[extractLogicalArea(addr)].type]);
		return Result::bug;
	}

	if(extractLogicalArea(addr) == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREE NODE operation on (log.) first Area at %X:%X", extractLogicalArea(addr), extractPage(addr));
		return Result::bug;
	}


	Result r;
	if(traceMask & PAFFS_TRACE_VERIFY_AS){
		if(dev->sumCache.getPageStatus(extractLogicalArea(addr), extractPage(addr),&r) == SummaryEntry::dirty){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "READ operation of obsoleted data at %X:%X", extractLogicalArea(addr), extractPage(addr));
			return Result::bug;
		}
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not verify Page status!");
		}
	}

	r = dev->driver->readPage(getPageNumber(addr, dev), node, sizeof(TreeNode));
	if(r != Result::ok){
		if(r == Result::biterrorCorrected){
			PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror");
		}else{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Error reading Treenode");
			return r;
		}
	}

	if(node->self != addr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Read Treenode at %X:%X, but its content stated that it was on %X:%X", extractLogicalArea(addr), extractPage(addr), extractLogicalArea(node->self), extractPage(node->self));
		return Result::bug;
	}

	return r;
}

Result DataIO::deleteTreeNode(TreeNode* node){
	return dev->sumCache.setPageStatus(extractLogicalArea(node->self), extractPage(node->self), SummaryEntry::dirty);
}

Result DataIO::writePageData(PageOffs pageFrom, PageOffs toPage, unsigned offs,
		unsigned bytes, const char* data, PageAddressCache &ac,
		unsigned* bytes_written, FileSize filesize, uint32_t &reservedPages){
	//Will be set to zero after offset is applied
	if(offs > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried applying an offset %d > %d", offs, dataBytesPerPage);
		return Result::bug;
	}
	*bytes_written = 0;
	Result res;
	for(unsigned int page = 0; page <= toPage - pageFrom; page++){
		bool misaligned = false;
		Result rBuf = dev->lasterr;
		dev->lasterr = Result::ok;
		dev->activeArea[AreaType::data] = dev->areaMgmt.findWritableArea(AreaType::data);
		if(dev->lasterr != Result::ok){
			//TODO: Return to a safe state by trying to resurrect dirty marked pages
			//		Mark fresh written pages as dirty. If old pages have been deleted,
			//		use the Journal to resurrect (not currently implemented)
			return dev->lasterr;
		}
		dev->lasterr = rBuf;

		//Handle Areas
		if(dev->areaMap[dev->activeArea[AreaType::data]].status == AreaStatus::empty){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			dev->areaMgmt.initArea(dev->activeArea[AreaType::data]);
		}

		//Prepare buffer and calculate bytes to write
		char* buf = &const_cast<char*>(data)[*bytes_written];
		unsigned int btw = bytes - *bytes_written;
		if((btw + offs) > dataBytesPerPage){
			btw = (btw+offs) > dataBytesPerPage ?
						dataBytesPerPage - offs :
						dataBytesPerPage;
		}

		Addr pageAddr;
		res = ac.getPage(page+pageFrom, &pageAddr);
		if(res != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Coud not get Page %u for write-back" PRIu32, page+pageFrom);
			return res;
		}

		if((btw + offs < dataBytesPerPage &&
			page*dataBytesPerPage + btw < filesize) ||		//End Misaligned
			offs > 0){										//Start Misaligned
			//we are misaligned, so fill write buffer with valid Data
			misaligned = true;
			buf = new char[dataBytesPerPage];
			memset(buf, 0x0, dataBytesPerPage);

			unsigned int btr = dataBytesPerPage;

			//limit maximum bytes to read if file is smaller than actual page
			if((pageFrom+1+page)*dataBytesPerPage > filesize){
				if(filesize > (pageFrom+page) * dataBytesPerPage)
					btr = filesize - (pageFrom+page) * dataBytesPerPage;
				else
					btr = 0;
			}

			if(pageAddr != 0	 //not an empty page TODO: doubled code
					&& pageAddr != combineAddress(0, unusedMarker)){  //not a skipped page (thus containing no information)
				//We are overriding real data, not just empty space
				unsigned int bytes_read = 0;
				Result r = readPageData(pageFrom+page, pageFrom+page, 0, btr, buf, ac, &bytes_read);
				if(r != Result::ok || bytes_read != btr){
					delete[] buf;
					return Result::bug;
				}
			}

			//Handle offset
			memcpy(&buf[offs], &data[*bytes_written], btw);

			//this is here, because btw will be modified
			*bytes_written += btw;

			//increase btw to whole page to write existing data back
			btw = btr > (offs + btw) ? btr : offs + btw;

			//offset is only applied to first page
			offs = 0;
		}else{
			//not misaligned, we are writing a whole page or a new page
			*bytes_written += btw;
		}

		//if we are overwriting existing data... TODO: doubled code
		if(pageAddr != 0	 //not an empty page
			&& pageAddr != combineAddress(0, unusedMarker)){  //not a skipped page (thus containing no information)
			//Mark old pages dirty
			//mark old Page in Areamap
			unsigned long oldArea = extractLogicalArea(pageAddr);
			unsigned long oldPage = extractPage(pageAddr);

			res = dev->sumCache.setPageStatus(oldArea, oldPage, SummaryEntry::dirty);
			if(res != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!", resultMsg[static_cast<int>(res)]);
				PAFFS_DBG_S(PAFFS_TRACE_WRITE, "At pagelistindex %" PRIu32 ", oldArea: %lu, oldPage: %lu", page+pageFrom, oldArea, oldPage);
			}
		}else{
			//or we will add a new page to this file
			reservedPages ++;
		}

		//find new page to write to
		unsigned int firstFreePage = 0;
		if(dev->areaMgmt.findFirstFreePage(&firstFreePage, dev->activeArea[AreaType::data]) == Result::nosp){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::data]);
			return Result::bug;
		}
		Addr pageAddress = combineAddress(dev->activeArea[AreaType::data], firstFreePage);
		res = dev->sumCache.setPageStatus(dev->activeArea[AreaType::data], firstFreePage, SummaryEntry::used);
		if(res != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!", resultMsg[static_cast<int>(res)]);
		}
		ac.setPage(page+pageFrom, pageAddress);
		res = dev->driver->writePage(getPageNumber(pageAddress, dev), buf, btw);

		if(misaligned)
			delete[] buf;

		PAFFS_DBG_S(PAFFS_TRACE_WRITE, "write r.P: %d/%d, phy.P: %llu", page+1, toPage+1, static_cast<long long unsigned int> (getPageNumber(pageAddress, dev)));
		if(res != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", static_cast<long long unsigned int> (getPageNumber(pageAddress, dev)));
			return Result::fail;
		}

		res = dev->areaMgmt.manageActiveAreaFull(&dev->activeArea[AreaType::data], AreaType::data);
		if(res != Result::ok)
			return res;

	}
	return Result::ok;
}

Result DataIO::readPageData(PageOffs pageFrom, PageOffs toPage, unsigned offs,
			unsigned bytes, char* data, PageAddressCache &ac,
			unsigned* bytes_read){
	char* wrap = data;
	bool misaligned = false;
	if(offs > 0){
		misaligned = true;
		wrap = new char[bytes + offs];
	}

	for(unsigned int page = 0; page <= toPage - pageFrom; page++){
		char* buf = &wrap[page*dataBytesPerPage];
		Addr pageAddr;
		Result r = ac.getPage(page+pageFrom, &pageAddr);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Coud not get Page %u for read" PRIu32, page+pageFrom);
			return r;
		}

		unsigned int btr = bytes + offs - *bytes_read;
		if(btr > dataBytesPerPage){
			btr = (bytes + offs) > (page+1)*dataBytesPerPage ?
						dataBytesPerPage :
						(bytes + offs) - page*dataBytesPerPage;
		}

		AreaPos area = extractLogicalArea(pageAddr);
		if(dev->areaMap[area].type != AreaType::data){
			if(pageAddr == combineAddress(0, unusedMarker)){
				//This Page is currently not written to flash
				//because it contains just empty space
				memset(buf, 0, btr);
				*bytes_read += btr;
				continue;
			}
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid area at %d:%d",\
					extractLogicalArea(pageAddr),
					extractPage(pageAddr));
			return Result::bug;
		}
		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			SummaryEntry e = dev->sumCache.getPageStatus(extractLogicalArea(pageAddr)
					,extractPage(pageAddr), &r);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load AreaSummary of area %d for verification!",
						extractLogicalArea(pageAddr));
			}else{
				if(e == SummaryEntry::dirty){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of outdated (dirty) data at %d:%d",
							extractLogicalArea(pageAddr),
							extractPage(pageAddr));
					return Result::bug;
				}

				if(e == SummaryEntry::free){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid (free) data at %d:%d",
							extractLogicalArea(pageAddr),
							extractPage(pageAddr));
					return Result::bug;
				}

				if(e >= SummaryEntry::error){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of data with invalid AreaSummary at area %d!",
							extractLogicalArea(pageAddr));
				}
			}
		}

		PageAbs addr = getPageNumber(pageAddr, dev);
		r = dev->driver->readPage(addr, buf, btr);
		if(r != Result::ok){
			if(r == Result::biterrorCorrected){
				//TODO rewrite page
				PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write corrected version back to flash.");
			}else{
				if(misaligned)
					delete[] wrap;
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read page, aborting pageData Read");
				return dev->lasterr = r;
			}
		}
		*bytes_read += btr;

	}

	if(misaligned) {
		memcpy(data, &wrap[offs], bytes);
		*bytes_read -= offs;
		delete[] wrap;
	}

	return Result::ok;
}

}

