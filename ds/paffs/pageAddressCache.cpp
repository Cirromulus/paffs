/*
 * pageAddressCache.cpp
 *
 *  Created on: 17.06.2017
 *      Author: urinator
 */

#include "pageAddressCache.hpp"
#include "device.hpp"
#include "dataIO.hpp"
#include "driver/driver.hpp"
#include <inttypes.h>
#include <cmath>

namespace paffs{

void AddrListCacheElem::setAddr(PageNo pos, Addr addr){
	if(pos >= addrsPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set pos %" PRIu32 " to addr,"
				" allowed < %u", pos, addrsPerPage);
	}
	cache[pos] = addr;
	dirty = true;
}
Addr AddrListCacheElem::getAddr(PageNo pos){
	if(pos >= addrsPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get pos %" PRIu32
				" allowed < %u", pos, addrsPerPage);
	}
	return cache[pos];
}



void PageAddressCache::setTargetInode(Inode* node){
	if(node != inode){
		for(AddrListCacheElem& elem : tripl){
			elem.active = false;
		}
		for(AddrListCacheElem& elem : doubl){
			elem.active = false;
		}
		singl.active = false;
	}
	inode = node;
}

Result PageAddressCache::getPage(PageNo page, Addr *addr){
	if(inode == nullptr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Page of null inode");
		return Result::bug;
	}
	if(page < directAddrCount){
		*addr = inode->direct[page];
		return Result::ok;
	}
	page -= directAddrCount;
	Result r;

	if(page < addrsPerPage){
		//First Indirection
		if(!singl.active){
			r = loadCacheElem(inode->indir, singl);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read first indirection!");
				return r;
			}
		}
		*addr = singl.getAddr(page);
		return Result::ok;
	}
	page -= addrsPerPage;
	PageNo addrPos;

	if(page < std::pow(addrsPerPage, 2)){
		r = loadPath(inode->d_indir, page, doubl, 1, addrPos);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load second indirection!");
			return r;
		}
		*addr = doubl[1].getAddr(addrPos);
		return Result::ok;
	}
	page -= std::pow(addrsPerPage, 2);

	if(page < std::pow(addrsPerPage, 3)){
		r = loadPath(inode->t_indir, page, tripl, 2, addrPos);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load third indirection!");
			return r;
		}
		*addr = tripl[2].getAddr(addrPos);
		return Result::ok;
	}

	PAFFS_DBG(PAFFS_TRACE_ERROR, "Get Page bigger than allowed! (was %u, should <%u)"
			, 0,0); //TODO: Actual calculation of values
	return Result::toobig;
}

Result PageAddressCache::setPage(PageNo page, Addr  addr){
	if(inode == nullptr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Page of null inode");
		return Result::bug;
	}
	if(page < directAddrCount){
		inode->direct[page] = addr;
		return Result::ok;
	}
	page -= directAddrCount;
	Result r;

	if(page < addrsPerPage){
		//First Indirection
		if(!singl.active){
			r = loadCacheElem(inode->indir, singl);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read first indirection!");
				return r;
			}
		}
		singl.setAddr(page, addr);
		return Result::ok;
	}
	page -= addrsPerPage;
	PageNo addrPos;

	if(page < std::pow(addrsPerPage, 2)){
		r = loadPath(inode->d_indir, page, doubl, 1, addrPos);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load second indirection!");
			return r;
		}
		doubl[1].setAddr(addrPos, addr);
		return Result::ok;
	}
	page -= std::pow(addrsPerPage, 2);

	if(page < std::pow(addrsPerPage, 3)){
		r = loadPath(inode->t_indir, page, tripl, 2, addrPos);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load third indirection!");
			return r;
		}
		tripl[2].setAddr(addrPos, addr);
		return Result::ok;
	}

	PAFFS_DBG(PAFFS_TRACE_ERROR, "Get Page bigger than allowed! (was %u, should <%u)"
			, 0,0); //TODO: Actual calculation of values
	return Result::toobig;
}

Result PageAddressCache::commit(){
	if(inode == nullptr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Page of null inode");
		return Result::bug;
	}

	Result r;
	r = commitPath(inode->indir, &singl, 0);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit first indirection!");
		return r;
	}

	r = commitPath(inode->d_indir, doubl, 1);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit second indirection!");
		return r;
	}

	r = commitPath(inode->t_indir, tripl, 2);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit third indirection!");
		return r;
	}

	return Result::ok;
}

/*
Result PageAddressCache::getPage(Inode *inode, Addr* &pageList, unsigned int fromPage,
		unsigned int toPage){
	//TODO: Read only fromPage-ToPage and not 0-ToPage
	(void) fromPage;
	pageList = inode->direct;
	if(toPage >= 11){
		if(toPage >= maxAddrs){
			//Would use second indirection layer, not yet implemented.
			PAFFS_DBG(PAFFS_TRACE_ERROR, "File would use %u pages, we currently support only %u", toPage, maxAddrs);
			return dev->lasterr = Result::nimpl;
		}
		unsigned filePages = inode->size ? inode->size / dataBytesPerPage : 0;
		if(inode->size % dataBytesPerPage != 0)
			filePages++;

		//Would use first indirection Layer
		PAFFS_DBG_S(PAFFS_TRACE_WRITE, "read uses first indirection layer");
		pageList = pageListBuffer;
		memcpy(pageList, inode->direct, 11 * sizeof(Addr));
		memset(&pageList[11], 0, (maxAddrs - 11) * sizeof(Addr));
		//Check if data from first indirection is available
		if(inode->indir != 0){
			if(filePages > maxAddrs){
				PAFFS_DBG(PAFFS_TRACE_BUG, "filesize is bigger than it could have been written");
				return Result::bug;
			}
			if(static_cast<int>(filePages) - 11 < 0){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "inode->indir != 0, but size"
					" is too small for indirection layer (%u Byte, "
					"so %d pages)", inode->size,
					filePages);
				return Result::bug;
			}
			PAFFS_DBG_S(PAFFS_TRACE_WRITE, "Reading additional %u addresses "
					"for indirection",filePages - 11);
			PageAbs addr = getPageNumber(inode->indir, dev);
			Result res = dev->driver->readPage(addr, &pageList[11], (filePages - 11) * sizeof(Addr));
			if(res != Result::ok){
				if(res == Result::biterrorCorrected){
					//TODO rewrite PageList or mark it as dirty
					PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet "
						"write corrected version back to flash.");
				}else{
					PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load existing addresses"
						" of first indirection layer");
					return res;
				}
			}
		}
	}
	return Result::ok;
}

//todo: Change this function to not need inode, and not distinguish between direct and indirect. Implement in new cache.
Result PageAddressCache::writePageList(Inode *inode, Addr& page, Addr* &pageList,
		unsigned int fromPage, unsigned int toPage){
	(void) fromPage;
	(void) toPage;
	if(pageList != inode->direct){
		memcpy(inode->direct, pageList, 11 * sizeof(Addr));
		unsigned filePages = inode->size ? inode->size / dataBytesPerPage + 1 : 0;
		if(filePages <= 11){
			//Pages have been deleted
			Result r = dev->sumCache.setPageStatus(extractLogicalArea(page), extractPage(page), SummaryEntry::dirty);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write AreaSummary for area %d,"
						" so no invalidation of data!", extractLogicalArea(page));
			}
			page = 0;
		}
		unsigned int bw;
		uint32_t reservedPages = 0;
		Result r = writePageData(0, 0, 0, filePages - 11 * sizeof(Addr),
				reinterpret_cast<char*>(&pageList[11]),
				&page, &bw, filePages - 11 * sizeof(Addr), reservedPages);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write indirection addresses");
			return r;
		}
	}
	return Result::ok;
} */

Result PageAddressCache::loadPath(Addr& anchor, PageNo pageOffs, AddrListCacheElem* start,
		unsigned char depth, PageNo &addrPos){
	Result r;
	PageNo path[3] = {0};
	for(unsigned int i = 0; i <= depth; i++){
		path[depth - i] = static_cast<unsigned int>(
				pageOffs / std::pow(addrsPerPage, i))
				% addrsPerPage;
		if(path[depth - i] >= addrsPerPage){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Miscalculated path for page %" PRIu32
					" (was %u, should < %u)", pageOffs, path[depth - i], addrsPerPage);
			return Result::bug;
		}
	}
	PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Resulting path: %" PRIu32 ":%" PRIu32 ":%" PRIu32,
			path[0], path[1], path[2]);


	if(!start[0].active){
		r = loadCacheElem(anchor, start[0]);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read elem 0 in second indirection!");
			return r;
		}
	}

	for(unsigned int i = 1; i <= depth; i++){
		if(start[i].active && start[i].positionInParent != path[i-1]){
			//We would override existing CacheElem
			r = commitElem(start[i-1], start[i]);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit elem depth %d"
						" at %" PRIu32 ":%" PRIu32 ":%" PRIu32, i, path[0], path[1], path[2]);
				return r;
			}
		}

		if(!start[i].active || start[i].positionInParent != path[i-1]){
			//Load if it was inactive or has just been committed
			r = loadCacheElem(start[i-1].getAddr(path[i-1]), start[i]);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read elem depth %d"
						" at %" PRIu32 ":%" PRIu32 ":%" PRIu32, i, path[0], path[1], path[2]);
				return r;
			}
			start[i].positionInParent = path[i-1];
		}
	}
	addrPos = path[depth];
	return Result::ok;
}

Result PageAddressCache::commitPath(Addr& anchor, AddrListCacheElem* path,
		unsigned char depth){
	Result r;
	for(unsigned int i = depth; i > 0; i--){
		if(path[i].dirty){
			r = commitElem(path[i-1], path[i]);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Elem in depth %u!", i);
				return r;
			}
		}
	}
	if(!path[0].dirty){
		return Result::ok;
	}

	r = writeCacheElem(anchor, path[0]);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Elem in depth 0!");
		return r;
	}
	path[0].dirty = false;
	return Result::ok;
}

Result PageAddressCache::commitElem(AddrListCacheElem &parent, AddrListCacheElem &elem){
	if(!elem.active){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to commit inactive Cache Elem");
		return Result::bug;
	}
	if(!elem.dirty){
		return Result::ok;
	}

	Result r = writeCacheElem(parent.cache[elem.positionInParent], elem);
	if(r != Result::ok){
		return r;
	}
	parent.dirty = true;
	return Result::ok;
}

Result  PageAddressCache::loadCacheElem(Addr from, AddrListCacheElem &elem){
	Result r = readAddrList(from, elem.cache);
	if(r == Result::biterrorCorrected){
		PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror in AddrList");
		elem.dirty = true;
		elem.active = true;
		return Result::ok;
	}else if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load Cache Elem");
		return r;
	}
	elem.active = true;
	elem.dirty = false;
	return Result::ok;
}

Result PageAddressCache::writeCacheElem(Addr &to, AddrListCacheElem &elem){
	Result r = writeAddrList(to, elem.cache);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Cache Elem");
		return r;
	}
	elem.dirty = false;
	return Result::ok;
}

Result PageAddressCache::readAddrList (Addr from, Addr list[addrsPerPage]){
	if(from == 0){
		//Not yet used
		memset(list, 0, addrsPerPage * sizeof(Addr));
		return Result::ok;
	}
	Result res = dev->driver->readPage(from,list, addrsPerPage * sizeof(Addr));
	if(res != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load existing addresses"
			" of first indirection layer");
		return res;
	}
	if(traceMask & PAFFS_TRACE_VERIFY_ALL){
		if(!isAddrListPlausible(list, addrsPerPage)){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Read of addrList inplausible!");
			return Result::fail;
		}
	}
	return res;
}

Result PageAddressCache::writeAddrList(Addr &to , Addr list[addrsPerPage]){
	Result r = dev->lasterr;
	dev->lasterr = Result::ok;
	dev->activeArea[AreaType::data] = dev->areaMgmt.findWritableArea(AreaType::data);
	if(dev->lasterr != Result::ok){
		//TODO: Reset former pagestatus, so that FS will be in a safe state
		return dev->lasterr;
	}
	if(dev->activeArea[AreaType::data] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
		return Result::bug;
	}
	dev->lasterr = r;

	if(to != 0){
		//We have to invalidate former position first
		r = dev->sumCache.setPageStatus(extractLogicalArea(to),
				extractPage(to), SummaryEntry::dirty);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not invalidate old Page! "
					"Ignoring Errors to continue...");
		}
	}

	unsigned int firstFreePage = 0;
	if(dev->areaMgmt.findFirstFreePage(&firstFreePage,
			dev->activeArea[AreaType::data]) == Result::nosp){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::index]);
		return dev->lasterr = Result::bug;
	}
	to = combineAddress(dev->activeArea[AreaType::index], firstFreePage);

	//Mark Page as used
	r = dev->sumCache.setPageStatus(dev->activeArea[AreaType::data],
			firstFreePage, SummaryEntry::used);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not mark Page as used!");
		return r;
	}

	r = dev->driver->writePage(getPageNumber(to, dev), reinterpret_cast<char*>(list),
			addrsPerPage * sizeof(Addr));
	if(r != Result::ok){
		//TODO: Revert Changes to PageStatus
		return r;
	}

	r = dev->areaMgmt.manageActiveAreaFull(&dev->activeArea[AreaType::data], AreaType::data);
	if(r != Result::ok)
		return r;

	return Result::ok;
}

bool PageAddressCache::isAddrListPlausible(Addr* addrList, size_t elems){
	for(unsigned i = 0; i < elems; i++){
		if(extractPage(addrList[i]) == unusedMarker)
			continue;

		if(extractPage(addrList[i]) > dataPagesPerArea){
			PAFFS_DBG(PAFFS_TRACE_BUG, "PageList elem %d Page is higher than possible "
					"(was %" PRIu32 ", should < %" PRIu32 ")", i, extractPage(addrList[i]), dataPagesPerArea);
			return false;
		}
		if(extractLogicalArea(addrList[i]) == 0 && extractPage(addrList[i]) != 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "PageList elem %d Area is 0, "
					"but Page is not (%" PRIu32 ")", i, extractPage(addrList[i]));
			return false;
		}
	}
	return true;
}

}

