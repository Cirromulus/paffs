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

	unsigned int pageFrom = offs/dev->param->dataBytesPerPage;
	unsigned int pageTo = (offs + bytes - 1) / dev->param->dataBytesPerPage;

	if(pageTo > 11){
		//Would use first indirection Layer
		PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Write would use first indirection layer, too big!");
		return dev->lasterr = Result::nimpl;
	}

	for(unsigned int i = 0; i < pageFrom; i++){
		if(inode->direct[i] == 0){
			//we jumped over pages, so mark them as not (yet) used
			inode->direct[i] = combineAddress(0, unusedMarker);
		}
	}

	//Will be set to zero after offset is applied
	unsigned int pageOffs = offs % dev->param->dataBytesPerPage;
	*bytes_written = 0;

	Result res;

	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){
		bool misaligned = false;
		dev->activeArea[AreaType::data] = dev->areaMgmt.findWritableArea(AreaType::data);
		if(dev->lasterr != Result::ok){
			//TODO: Return to a safe state by trying to resurrect dirty marked pages
			//		Mark fresh written pages as dirty. If old pages have been deleted,
			//		use the Journal to resurrect (not currently used)
			return dev->lasterr;
		}

		//Handle Areas
		if(dev->areaMap[dev->activeArea[AreaType::data]].status == AreaStatus::empty){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			dev->areaMgmt.initArea(dev->activeArea[AreaType::data]);
		}
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

		//Prepare buffer and calculate bytes to write
		//FIXME: If write has offset, misaligned is still false
		//so no leading zeros are inserted
		char* buf = &const_cast<char*>(data)[page*dev->param->dataBytesPerPage];
		unsigned int btw = bytes - *bytes_written;
		if((bytes+pageOffs) > dev->param->dataBytesPerPage){
			btw = (bytes+pageOffs) > (page+1)*dev->param->dataBytesPerPage ?
						dev->param->dataBytesPerPage - pageOffs :
						bytes - page*dev->param->dataBytesPerPage;
		}



		if((inode->direct[page+pageFrom] != 0
				&& inode->direct[page+pageFrom] != combineAddress(0, unusedMarker))
				|| pageOffs != 0){
			//We are overriding existing data
			//mark old Page in Areamap
			unsigned long oldArea = extractLogicalArea(inode->direct[page+pageFrom]);
			unsigned long oldPage = extractPage(inode->direct[page+pageFrom]);


			if((btw + pageOffs < dev->param->dataBytesPerPage) &&
				(page*dev->param->dataBytesPerPage + btw < inode->size) ||  //End Misaligned
				pageOffs > 0 && page == 0){									//Start Misaligned

				//fill write buffer with valid Data
				misaligned = true;
				buf = new char[dev->param->dataBytesPerPage];
				memset(buf, 0x0, dev->param->dataBytesPerPage);

				unsigned int btr = dev->param->dataBytesPerPage;

				//limit maximum bytes to read if file is smaller than actual page
				if((pageFrom+1+page)*dev->param->dataBytesPerPage > inode->size){
					btr = inode->size - (pageFrom+page) * dev->param->dataBytesPerPage;
				}

				if(inode->direct[page+pageFrom] != 0
						&& inode->direct[page+pageFrom] != combineAddress(0, unusedMarker)){
					//We are overriding real data, not just empty space
					unsigned int bytes_read = 0;
					Result r = readInodeData(inode, (pageFrom+page)*dev->param->dataBytesPerPage, btr, &bytes_read, buf);
					if(r != Result::ok || bytes_read != btr){
						delete[] buf;
						return Result::bug;
					}
				}

				//Handle pageOffset
				memcpy(&buf[pageOffs], &data[*bytes_written], btw);

				//this is here, because btw will be modified
				*bytes_written += btw;

				//increase btw to whole page to write existing data back
				btw = btr > (pageOffs + btw) ? btr : pageOffs + btw;

				//pageoffset is only at applied to first page
				pageOffs = 0;
			}else{
				//not misaligned
				*bytes_written += btw;
			}

			//if we overwrote an existing page
			if(inode->direct[page+pageFrom] != 0
					&& inode->direct[page+pageFrom] != combineAddress(0, unusedMarker)){
				//Mark old pages dirty
				res = dev->sumCache.setPageStatus(oldArea, oldPage, SummaryEntry::dirty);
				if(res != Result::ok){
					PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!", resultMsg[static_cast<int>(res)]);
				}
			}
		}else{
			//we are writing to a new page
			*bytes_written += btw;
			inode->reservedSize += dev->param->dataBytesPerPage;
		}
		inode->direct[page+pageFrom] = pageAddress;

		res = dev->driver->writePage(getPageNumber(pageAddress, dev), buf, btw);

		if(misaligned)
			delete[] buf;

		PAFFS_DBG_S(PAFFS_TRACE_WRITE, "write r.P: %d/%d, phy.P: %llu", page+1, pageTo+1, static_cast<long long unsigned int> (getPageNumber(pageAddress, dev)));
		if(res != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", static_cast<long long unsigned int> (getPageNumber(pageAddress, dev)));
			return Result::fail;
		}

		res = dev->areaMgmt.manageActiveAreaFull(&dev->activeArea[AreaType::data], AreaType::data);
		if(res != Result::ok)
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
	unsigned int pageFrom = offs/dev->param->dataBytesPerPage;
	unsigned int pageTo = (offs + bytes - 1) / dev->param->dataBytesPerPage;
	unsigned int pageOffs = offs % dev->param->dataBytesPerPage;


	if(offs + bytes > inode->size){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read bigger than size of object! (was: %d, max: %lu)", offs+bytes, static_cast<long unsigned>(inode->size));
		//TODO: return less bytes_read
		return Result::nimpl;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return Result::nimpl;
	}

	char* wrap = data;
	bool misaligned = false;
	if(pageOffs > 0){
		misaligned = true;
		wrap = new char[bytes + pageOffs];
	}

	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){
		char* buf = &wrap[page*dev->param->dataBytesPerPage];

		unsigned int btr = bytes + pageOffs - *bytes_read;
		if(btr > dev->param->dataBytesPerPage){
			btr = (bytes + pageOffs) > (page+1)*dev->param->dataBytesPerPage ?
						dev->param->dataBytesPerPage :
						(bytes + pageOffs) - page*dev->param->dataBytesPerPage;
		}

		AreaPos area = extractLogicalArea(inode->direct[pageFrom + page]);
		if(dev->areaMap[area].type != AreaType::data){
			if(inode->direct[pageFrom + page] == combineAddress(0, unusedMarker)){
				//This Page is currently not written to flash
				//because it contains just empty space
				memset(buf, 0, btr);
				*bytes_read += btr;
				continue;
			}
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid area at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return Result::bug;
		}
		Result r;
		if(traceMask && PAFFS_TRACE_VERIFY_AS){
			SummaryEntry e = dev->sumCache.getPageStatus(extractLogicalArea(inode->direct[page + pageFrom])
					,extractPage(inode->direct[page + pageFrom]), &r);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load AreaSummary of area %d for verification!", extractLogicalArea(inode->direct[page + pageFrom]));
			}else{
				if(e == SummaryEntry::dirty){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
					return Result::bug;
				}

				if(e == SummaryEntry::free){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid (free) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
					return Result::bug;
				}

				if(e >= SummaryEntry::error){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of data with invalid AreaSummary at area %d!", extractLogicalArea(inode->direct[page + pageFrom]));
				}
			}
		}

		Addr addr = getPageNumber(inode->direct[page + pageFrom], dev);
		r = dev->driver->readPage(addr, buf, btr);
		if(r != Result::ok){
			if(misaligned)
				delete[] wrap;
			return dev->lasterr = r;
		}
		*bytes_read += btr;

	}

	if(misaligned) {
		memcpy(data, &wrap[pageOffs], bytes);
		*bytes_read -= pageOffs;
		delete[] wrap;
	}

	return Result::ok;
}


//inode->size and inode->reservedSize is altered.
Result DataIO::deleteInodeData(Inode* inode, unsigned int offs){
	//TODO: This calculation contains errors in border cases
	unsigned int pageFrom = offs/dev->param->dataBytesPerPage;
	unsigned int pageTo = (inode->size - 1) / dev->param->dataBytesPerPage;

	if(inode->size < offs){
		//Offset bigger than actual filesize
		return Result::einval;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return Result::nimpl;
	}


	inode->size = offs;

	if(inode->reservedSize == 0)
		return Result::ok;

	if(inode->size >= inode->reservedSize - dev->param->dataBytesPerPage)
		//doesn't leave a whole page blank
		return Result::ok;


	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){

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

		inode->reservedSize -= dev->param->dataBytesPerPage;
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
	if(sizeof(TreeNode) > dev->param->dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page"
				" (Was %u, should %u)", sizeof(TreeNode), dev->param->dataBytesPerPage);
		return Result::bug;
	}

	if(node->self != 0){
		//We have to invalidate former position first
		Result r = dev->sumCache.setPageStatus(extractLogicalArea(node->self), extractPage(node->self), SummaryEntry::dirty);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not invalidate old Page!");
			return r;
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
	if(sizeof(TreeNode) > dev->param->dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page (Was %u, should %u)", sizeof(TreeNode), dev->param->dataBytesPerPage);
		return Result::bug;
	}

	if(dev->areaMap[extractLogicalArea(addr)].type != AreaType::index){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREEENODE operation on %s!", area_names[dev->areaMap[extractLogicalArea(addr)].type]);
		return Result::bug;
	}

	if(extractLogicalArea(addr) == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREE NODE operation on (log.) first Area at %X:%X", extractLogicalArea(addr), extractPage(addr));
		return Result::bug;
	}


	Result r;
	if(traceMask && PAFFS_TRACE_VERIFY_AS){
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




}

