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
	Addr *pageList = 0;
	BitList<maxAddrs> modified;

	res = readPageList(inode, pageList, pageFrom, toPage);
	if(res != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "could not read page Address list");
		return res;
	}
	unsigned pageoffs = offs % dataBytesPerPage;
	res = writePageData(pageFrom, toPage, pageoffs, bytes, data,
			pageList, bytes_written, inode, modified);
	if(res != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "could not write pageData");
		return res;
	}

	res = writePageList(inode, pageList, modified, pageFrom, toPage);
	if(res != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "could not write back page Address list");
		return res;
	}

	if(inode->size < *bytes_written + offs)
		inode->size = *bytes_written + offs;

	return dev->tree.updateExistingInode(inode);
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
		//TODO: return less bytes_read
		return Result::nimpl;
	}

	if(toPage > 11){
		//todo Read indirection Layers
		return Result::toobig;
	}

	return readPageData(pageFrom, toPage, offs % dataBytesPerPage, bytes, data, inode->direct, bytes_read);
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

	if(toPage > 11){
		//todo Read indirection Layers
		return Result::toobig;
	}


	inode->size = offs;

	if(inode->reservedSize == 0)
		return Result::ok;

	if(inode->size >= inode->reservedSize - dataBytesPerPage)
		//doesn't leave a whole page blank
		return Result::ok;


	for(unsigned int page = 0; page <= toPage - pageFrom; page++){

		unsigned int area = extractLogicalArea(inode->direct[page + pageFrom]);
		unsigned int relPage = extractPage(inode->direct[page + pageFrom]);

		if(dev->areaMap[area].type != AreaType::data){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of invalid area at %d:%d",
					extractLogicalArea(inode->direct[page + pageFrom]),
					extractPage(inode->direct[page + pageFrom]));
			return Result::bug;
		}

		Result r;
		if(dev->sumCache.getPageStatus(area, relPage, &r) == SummaryEntry::dirty){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of outdated (dirty)"
					" data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),
					extractPage(inode->direct[page + pageFrom]));
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

		inode->reservedSize -= dataBytesPerPage;
		inode->direct[page+pageFrom] = 0;

	}

	return Result::ok;
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

	if(node->self != 0){
		//We have to invalidate former position first
		Result r = dev->sumCache.setPageStatus(extractLogicalArea(node->self), extractPage(node->self), SummaryEntry::dirty);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not invalidate old Page! Ignoring Errors to continue...");
			//return r;
		}
	}

	dev->lasterr = Result::ok;
	dev->activeArea[AreaType::index] = dev->areaMgmt.findWritableArea(AreaType::index);
	if(dev->lasterr != Result::ok){
		//TODO: Reset former pagestatus, so that FS will be in a safe state
		return dev->lasterr;
	}

	if(dev->activeArea[AreaType::index] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
		return Result::bug;
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
	if(r != Result::ok)
		return r;

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
	if(r != Result::ok)
		return r;

	if(node->self != addr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Read Treenode at %X:%X, but its content stated that it was on %X:%X", extractLogicalArea(addr), extractPage(addr), extractLogicalArea(node->self), extractPage(node->self));
		return Result::bug;
	}

	return Result::ok;
}

Result DataIO::deleteTreeNode(TreeNode* node){
	return dev->sumCache.setPageStatus(extractLogicalArea(node->self), extractPage(node->self), SummaryEntry::dirty);
}


Result DataIO::writePageData(PageOffs pageFrom, PageOffs toPage, unsigned offs,
		unsigned bytes, const char* data, Addr *pageList,
		unsigned* bytes_written, Inode* inode, BitList<maxAddrs> &modified){
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
		char* buf = &const_cast<char*>(data)[page*dataBytesPerPage];
		unsigned int btw = bytes - *bytes_written;
		if((bytes+offs) > dataBytesPerPage){
			btw = (bytes+offs) > (page+1)*dataBytesPerPage ?
						dataBytesPerPage - offs :
						bytes - page*dataBytesPerPage;
		}

		if((btw + offs < dataBytesPerPage &&
			page*dataBytesPerPage + btw < inode->size) ||	//End Misaligned
			(offs > 0 && page == 0)){						//Start Misaligned
			//we are misaligned, so fill write buffer with valid Data
			misaligned = true;
			buf = new char[dataBytesPerPage];
			memset(buf, 0x0, dataBytesPerPage);

			unsigned int btr = dataBytesPerPage;

			//limit maximum bytes to read if file is smaller than actual page
			if((pageFrom+1+page)*dataBytesPerPage > inode->size){
				if(inode->size > (pageFrom+page) * dataBytesPerPage)
					btr = inode->size - (pageFrom+page) * dataBytesPerPage;
				else
					btr = 0;
			}

			if(pageList[page+pageFrom] != 0	 //not an empty page
					&& pageList[page+pageFrom] != combineAddress(0, unusedMarker)){  //not a skipped page (thus containing no information)
				//We are overriding real data, not just empty space
				unsigned int bytes_read = 0;
				Result r = readPageData(pageFrom+page, pageFrom+page, 0, btr, buf, pageList, &bytes_read);
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
			//not misaligned, we are writing a whole page
			*bytes_written += btw;
		}

		//if we are overwriting existing data...
		if(pageList[page+pageFrom] != 0	 //not an empty page
			&& pageList[page+pageFrom] != combineAddress(0, unusedMarker)){  //not a skipped page (thus containing no information)
			//Mark old pages dirty
			//mark old Page in Areamap
			unsigned long oldArea = extractLogicalArea(pageList[page+pageFrom]);
			unsigned long oldPage = extractPage(pageList[page+pageFrom]);

			res = dev->sumCache.setPageStatus(oldArea, oldPage, SummaryEntry::dirty);
			if(res != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!", resultMsg[static_cast<int>(res)]);
			}
		}else{
			//or we will add a new page to this file
			inode->reservedSize += dataBytesPerPage;
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
		pageList[page+pageFrom] = pageAddress;
		modified.setBit(page+pageFrom);

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
			unsigned bytes, char* data, Addr *pageList,
			unsigned* bytes_read){
	char* wrap = data;
	bool misaligned = false;
	if(offs > 0){
		misaligned = true;
		wrap = new char[bytes + offs];
	}

	for(unsigned int page = 0; page <= toPage - pageFrom; page++){
		char* buf = &wrap[page*dataBytesPerPage];

		unsigned int btr = bytes + offs - *bytes_read;
		if(btr > dataBytesPerPage){
			btr = (bytes + offs) > (page+1)*dataBytesPerPage ?
						dataBytesPerPage :
						(bytes + offs) - page*dataBytesPerPage;
		}

		AreaPos area = extractLogicalArea(pageList[pageFrom + page]);
		if(dev->areaMap[area].type != AreaType::data){
			if(pageList[pageFrom + page] == combineAddress(0, unusedMarker)){
				//This Page is currently not written to flash
				//because it contains just empty space
				memset(buf, 0, btr);
				*bytes_read += btr;
				continue;
			}
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid area at %d:%d", extractLogicalArea(pageList[page + pageFrom]),extractPage(pageList[page + pageFrom]));
			return Result::bug;
		}
		Result r;
		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			SummaryEntry e = dev->sumCache.getPageStatus(extractLogicalArea(pageList[page + pageFrom])
					,extractPage(pageList[page + pageFrom]), &r);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load AreaSummary of area %d for verification!", extractLogicalArea(pageList[page + pageFrom]));
			}else{
				if(e == SummaryEntry::dirty){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(pageList[page + pageFrom]),extractPage(pageList[page + pageFrom]));
					return Result::bug;
				}

				if(e == SummaryEntry::free){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid (free) data at %d:%d", extractLogicalArea(pageList[page + pageFrom]),extractPage(pageList[page + pageFrom]));
					return Result::bug;
				}

				if(e >= SummaryEntry::error){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of data with invalid AreaSummary at area %d!", extractLogicalArea(pageList[page + pageFrom]));
				}
			}
		}

		PageAbs addr = getPageNumber(pageList[page + pageFrom], dev);
		r = dev->driver->readPage(addr, buf, btr);
		if(r != Result::ok){
			if(misaligned)
				delete[] wrap;
			return dev->lasterr = r;
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

Result DataIO::readPageList(Inode *inode, Addr* &pageList, unsigned int fromPage,
		unsigned int toPage){
	pageList = inode->direct;
	if(toPage > 11){
		if(toPage > 11 + dataBytesPerPage / sizeof(Addr)){
			//Would use second indirection layer, not yet implemented.
			return dev->lasterr = Result::nimpl;
		}
		//FIXME: This return is just for green screen appearance
		return dev->lasterr = Result::toobig;
		//Would use first indirection Layer
		PAFFS_DBG_S(PAFFS_TRACE_WRITE, "Write uses first indirection layer");
		pageList = new Addr[toPage];
		memcpy(pageList, inode->direct, 11 * sizeof(Addr));
		//Check if data from first indirection is available
		if(inode->indir != 0){
			int atr = (inode->reservedSize / dataBytesPerPage - 11);
			if(atr < 0){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "inode->indir != 0, but reservedSize"
					" is too small for indirection layer (%u Byte, "
					"so %d pages)", inode->reservedSize,
					inode->reservedSize / dataBytesPerPage);
				return Result::bug;
			}
			//TODO: read all addresses in page for write-back
			PageAbs addr = getPageNumber(inode->indir, dev);
			Result res = dev->driver->readPage(addr, &pageList[11], atr * sizeof(Addr));
			if(res != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load existing addresses"
						" of first indirection layer");
				return res;
			}
			memset(&pageList[11+atr], 0, toPage - 11+atr);
		}else{
			memset(&pageList[1], 0, toPage - 11);
		}
	}

	for(unsigned int i = 0; i < fromPage; i++){
		if(pageList[i] == 0){
			//we jumped over pages, so mark them as not (yet) used
			pageList[i] = combineAddress(0, unusedMarker);
		}
	}
	return Result::ok;
}

Result DataIO::writePageList(Inode *inode, Addr* &pageList, BitList<maxAddrs> &modified,
		unsigned int fromPage, unsigned int toPage){
	(void) modified;
	(void) fromPage;
	(void) toPage;
	if(pageList != inode->direct){
		return Result::nimpl;
	}
	return Result::ok;
}

}

