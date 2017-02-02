/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#include "driver/driver.hpp"
#include "area.hpp"
#include <stdlib.h>
#include <string.h>
#include "dataIO.hpp"

namespace paffs{



//modifies inode->size and inode->reserved size as well
Result writeInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_written,
					const char* data, Dev* dev){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Write size 0! Bug?");
		return lasterr = Result::einval;
	}

	unsigned int pageFrom = offs/dev->param->data_bytes_per_page;
	unsigned int pageTo = (offs + bytes - 1) / dev->param->data_bytes_per_page;

	if(pageTo - pageFrom > 11){
		//Would use first indirection Layer
		PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Write would use first indirection layer, too big!");
		return lasterr = Result::nimpl;
	}

	unsigned int pageOffs = offs % dev->param->data_bytes_per_page;
	*bytes_written = 0;

	Result res;

	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){
		bool misaligned = false;
		dev->activeArea[AreaType::data] = findWritableArea(AreaType::data, dev);
		if(lasterr != Result::ok){
			return lasterr;
		}

		//Handle Areas
		if(dev->areaMap[dev->activeArea[AreaType::data]].status == AreaStatus::empty){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			initArea(dev, dev->activeArea[AreaType::data]);
		}
		unsigned int firstFreePage = 0;
		if(findFirstFreePage(&firstFreePage, dev, dev->activeArea[AreaType::data]) == Result::nosp){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::data]);
			return Result::bug;
		}
		Addr pageAddress = combineAddress(dev->activeArea[AreaType::data], firstFreePage);

		res = dev->areaMap[dev->activeArea[AreaType::data]].setPageStatus(firstFreePage, SummaryEntry::used);
		if(res != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!", resultMsg[(int)res]);
		}

		//Prepare buffer and calculate bytes to write
		char* buf = &((char*)data)[page*dev->param->data_bytes_per_page];
		unsigned int btw = bytes - *bytes_written;
		if((bytes+pageOffs) > dev->param->data_bytes_per_page){
			btw = (bytes+pageOffs) > (page+1)*dev->param->data_bytes_per_page ?
						dev->param->data_bytes_per_page - pageOffs :
						bytes - page*dev->param->data_bytes_per_page;
		}



		if(inode->direct[page+pageFrom] != 0){
			//We are overriding existing data
			//mark old Page in Areamap
			unsigned long oldArea = extractLogicalArea(inode->direct[page+pageFrom]);
			unsigned long oldPage = extractPage(inode->direct[page+pageFrom]);


			if((btw + pageOffs < dev->param->data_bytes_per_page &&
				page*dev->param->data_bytes_per_page + btw < inode->size) ||  //End Misaligned
				(pageOffs > 0 && page == 0)){				//Start Misaligned

				//fill write buffer with valid Data
				misaligned = true;
				buf = (char*)malloc(dev->param->data_bytes_per_page);
				memset(buf, 0xFF, dev->param->data_bytes_per_page);

				unsigned int btr = dev->param->data_bytes_per_page;

				if((pageFrom+1+page)*dev->param->data_bytes_per_page > inode->size){
					btr = inode->size - (pageFrom+page) * dev->param->data_bytes_per_page;
				}

				unsigned int bytes_read = 0;
				Result r = readInodeData(inode, (pageFrom+page)*dev->param->data_bytes_per_page, btr, &bytes_read, buf, dev);
				if(r != Result::ok || bytes_read != btr){
					free(buf);
					return Result::bug;
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

			//Mark old pages dirty
			res = dev->areaMap[oldArea].setPageStatus(oldPage, SummaryEntry::dirty);
			if(res != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set Pagestatus bc. %s. This is not handled. Expect Errors!", resultMsg[(int)res]);
			}

		}else{
			//we are writing to a new page
			*bytes_written += btw;
			inode->reservedSize += dev->param->data_bytes_per_page;
		}
		inode->direct[page+pageFrom] = pageAddress;

		res = dev->driver->writePage(getPageNumber(pageAddress, dev), buf, btw);

		if(misaligned)
			free(buf);

		PAFFS_DBG_S(PAFFS_TRACE_WRITE, "write r.P: %d/%d, phy.P: %llu", page+1, pageTo+1, (long long unsigned int) getPageNumber(pageAddress, dev));
		if(res != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", (long long unsigned int) getPageNumber(pageAddress, dev));
			return Result::fail;
		}

		res = manageActiveAreaFull(dev, &dev->activeArea[AreaType::data], AreaType::data);
		if(res != Result::ok)
			return res;

	}

	if(inode->size < *bytes_written + offs)
		inode->size = *bytes_written + offs;

	return updateExistingInode(dev, inode);
}
Result readInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, Dev* dev){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read size 0! Bug?");
		return lasterr = Result::einval;
	}

	*bytes_read = 0;
	unsigned int pageFrom = offs/dev->param->data_bytes_per_page;
	unsigned int pageTo = (offs + bytes - 1) / dev->param->data_bytes_per_page;
	unsigned int pageOffs = offs % dev->param->data_bytes_per_page;


	if(offs + bytes > inode->size){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read bigger than size of object! (was: %d, max: %lu)", offs+bytes, (long unsigned) inode->size);
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
		wrap = (char*) malloc(bytes + offs);
	}

	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){
		char* buf = &wrap[page*dev->param->data_bytes_per_page];

		unsigned int btr = bytes + pageOffs - *bytes_read;
		if(btr > dev->param->data_bytes_per_page){
			btr = (bytes + pageOffs) > (page+1)*dev->param->data_bytes_per_page ?
						dev->param->data_bytes_per_page :
						(bytes + pageOffs) - page*dev->param->data_bytes_per_page;
		}

		AreaPos area = extractLogicalArea(inode->direct[page + pageFrom]);
		if(dev->areaMap[area].type != AreaType::data){
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid area at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return Result::bug;
		}
		Result r;
		if(trace_mask && PAFFS_TRACE_VERIFY_AS){
			SummaryEntry e = dev->areaMap[extractLogicalArea(inode->direct[page + pageFrom])]
										  .getPageStatus(extractPage(inode->direct[page + pageFrom]), &r);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load AreaSummary of area %d for verification!", extractLogicalArea(inode->direct[page + pageFrom]));
			}else{
				if(e == SummaryEntry::dirty){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
					return Result::bug;
				}

				if(e == SummaryEntry::free){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid (SummaryEntry::free) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
					return Result::bug;
				}

				if(e >= SummaryEntry::error){
					PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of data with invalid AreaSummary at area %d!", extractLogicalArea(inode->direct[page + pageFrom]));
				}
			}
		}

		unsigned long long addr = getPageNumber(inode->direct[page + pageFrom], dev);
		r = dev->driver->readPage(addr, buf, btr);
		if(r != Result::ok){
			if(misaligned)
				free (wrap);
			return lasterr = r;
		}
		*bytes_read += btr;

	}

	if(misaligned) {
		memcpy(data, &wrap[pageOffs], bytes);
		*bytes_read -= pageOffs;
		free (wrap);
	}

	return Result::ok;
}


//inode->size and inode->reservedSize is altered.
Result deleteInodeData(Inode* inode, Dev* dev, unsigned int offs){
	//TODO: This calculation contains errors in border cases
	unsigned int pageFrom = offs/dev->param->data_bytes_per_page;
	unsigned int pageTo = (inode->size - 1) / dev->param->data_bytes_per_page;

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

	if(inode->size >= inode->reservedSize - dev->param->data_bytes_per_page)
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
		if(dev->areaMap[area].getPageStatus(relPage, &r) == SummaryEntry::dirty){
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
		r = dev->areaMap[area].setPageStatus(relPage, SummaryEntry::dirty);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write AreaSummary for area %d,"
					" so no invalidation of data!", area);
			return r;
		}

		inode->reservedSize -= dev->param->data_bytes_per_page;
		inode->direct[page+pageFrom] = 0;

	}

	return Result::ok;
}

//Does not change addresses in parent Nodes
Result writeTreeNode(Dev* dev, TreeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode NULL");
				return Result::bug;
	}
	if(sizeof(TreeNode) > dev->param->data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page"
				" (Was %u, should %u)", sizeof(TreeNode), dev->param->data_bytes_per_page);
		return Result::bug;
	}

	if(node->self != 0){
		//We have to invalidate former position first
		Result r = dev->areaMap[extractLogicalArea(node->self)].setPageStatus(extractPage(node->self), SummaryEntry::dirty);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not invalidate old Page!");
			return r;
		}
	}

	lasterr = Result::ok;
	dev->activeArea[AreaType::index] = findWritableArea(AreaType::index, dev);
	if(lasterr != Result::ok){
		return lasterr;
	}

	if(dev->activeArea[AreaType::index] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
		return Result::bug;
	}

	unsigned int firstFreePage = 0;
	if(findFirstFreePage(&firstFreePage, dev, dev->activeArea[AreaType::index]) == Result::nosp){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::index]);
		return lasterr = Result::bug;
	}
	Addr addr = combineAddress(dev->activeArea[AreaType::index], firstFreePage);
	node->self = addr;

	//Mark Page as used
	Result r = dev->areaMap[dev->activeArea[AreaType::index]].setPageStatus(firstFreePage, SummaryEntry::used);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not mark Page as used!");
		return r;
	}


	r = dev->driver->writePage(getPageNumber(node->self, dev), node, sizeof(TreeNode));
	if(r != Result::ok)
		return r;

	r = manageActiveAreaFull(dev, &dev->activeArea[AreaType::index], AreaType::index);
	if(r != Result::ok)
		return r;

	return Result::ok;
}

Result readTreeNode(Dev* dev, Addr addr, TreeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode NULL");
		return Result::bug;
	}
	if(sizeof(TreeNode) > dev->param->data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page (Was %u, should %u)", sizeof(TreeNode), dev->param->data_bytes_per_page);
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
	if(trace_mask && PAFFS_TRACE_VERIFY_AS){
		if(dev->areaMap[extractLogicalArea(addr)].getPageStatus(extractPage(addr),&r) == SummaryEntry::dirty){
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

Result deleteTreeNode(Dev* dev, TreeNode* node){
	return dev->areaMap[extractLogicalArea(node->self)].setPageStatus(extractPage(node->self), SummaryEntry::dirty);
}




}

