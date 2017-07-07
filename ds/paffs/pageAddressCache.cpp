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

Result PageAddressCache::setTargetInode(Inode* node){
	if(node == inode){
		return Result::ok;
	}
	PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Set new target inode");
	if(isDirty()){
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Target Inode differs, committing old Inode");
		Result r = commit();
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit old Inode, aborting");
			return r;
		}
	}
	for(AddrListCacheElem& elem : tripl){
		elem.active = false;
	}
	for(AddrListCacheElem& elem : doubl){
		elem.active = false;
	}
	singl.active = false;
	inode = node;
	return Result::ok;
}

Result PageAddressCache::getPage(PageNo page, Addr *addr){
	if(inode == nullptr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Page of null inode");
		return Result::bug;
	}
	if(traceMask & PAFFS_TRACE_VERBOSE){
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "GetPage at %" PRIu32, page);
	}

	if(page < directAddrCount){
		*addr = inode->direct[page];
		return Result::ok;
	}
	page -= directAddrCount;
	Result r;

	if(page < addrsPerPage){
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Accessing first indirection at %" PRIu32, page);
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
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Accessing second indirection at %" PRIu32, page);
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
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Accessing third indirection at %" PRIu32, page);
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

Result PageAddressCache::setPage(PageNo page, Addr addr){
	if(inode == nullptr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Page of null inode");
		return Result::bug;
	}
	if(dev->readOnly){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried setting PageAddress in readOnly mode!");
		return Result::bug;
	}

	if(traceMask & PAFFS_TRACE_VERBOSE){
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "SetPage to %" PRIu32 ":%" PRIu32 " at %" PRIu32,
			extractLogicalArea(addr), extractPage(addr), page);
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

Result PageAddressCache::deletePage(PageNo pageFrom, PageNo pageTo){
	if(inode == nullptr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Page of null inode");
		return Result::bug;
	}
	if(pageFrom > pageTo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "pageFrom bigger than pageTo! (%u > %u)", pageFrom, pageTo);
		return Result::bug;
	}
	if(pageTo > inode->size / dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "pageTo bigger than filesize! (%u > %u)", pageTo, inode->size / dataBytesPerPage);
		return Result::bug;
	}

	PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "delete Page %u-%u", pageFrom, pageTo);

	if(pageFrom < directAddrCount){
		//Limits the write to direct array
		unsigned char count = pageTo >= directAddrCount ?
				directAddrCount - pageFrom : pageTo - pageFrom + 1;
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "overwriting %u pages in direct", count);
		memset(&inode->direct[pageFrom], 0, count * sizeof(Addr));
	}

	if(pageTo < directAddrCount)
		return Result::ok;
	pageFrom -= pageFrom < directAddrCount ? 0 : directAddrCount;
	pageTo   -= directAddrCount;

	Result r;
	if(pageFrom < addrsPerPage){
		//First Indirection
		PageNo limitedPageTo = pageTo > addrsPerPage - 1 ? addrsPerPage - 1 : addrsPerPage;
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Limiting pageTo to %" PRIu32 " for first indir", limitedPageTo);
		r = deletePath(inode->indir, pageFrom, limitedPageTo, &singl, 0);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete in first indirection!");
			return r;
		}
	}

	if(pageTo < std::pow(directAddrCount, 2))
		return Result::ok;
	pageFrom -= pageFrom < std::pow(directAddrCount, 2) ? 0 : std::pow(directAddrCount, 2);
	pageTo   -= std::pow(directAddrCount, 2);

	//Second layer
	PAFFS_DBG(PAFFS_TRACE_ERROR, "Deleting second layer not implemented.");

	return Result::nimpl;
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

	return dev->tree.updateExistingInode(inode);
}

bool PageAddressCache::isDirty(){
	if(singl.dirty){
		return true;
	}
	if(doubl[0].dirty || doubl[1].dirty){
		return true;
	}
	if(tripl[0].dirty || tripl[1].dirty || tripl[2].dirty){
		return true;
	}
	return false;
}

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

Result PageAddressCache::deletePath(Addr& anchor, PageNo pageFrom, PageNo pageTo,
		AddrListCacheElem* start, unsigned char depth){
	//Result r;
	PageNo fromPath[3] = {0};
	PageNo   toPath[3] = {0};
	for(unsigned int i = 0; i <= depth; i++){
		fromPath[depth - i] = static_cast<unsigned int>(
				pageFrom / std::pow(addrsPerPage, i))
				% addrsPerPage;
		if(fromPath[depth - i] >= addrsPerPage){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Miscalculated frompath for page %" PRIu32
					" (was %u, should < %u)", pageFrom, fromPath[depth - i], addrsPerPage);
			return Result::bug;
		}
		toPath[depth - i] = static_cast<unsigned int>(
				pageTo / std::pow(addrsPerPage, i))
				% addrsPerPage;
		if(toPath[depth - i] >= addrsPerPage){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Miscalculated topath for page %" PRIu32
					" (was %u, should < %u)", pageTo, toPath[depth - i], addrsPerPage);
			return Result::bug;
		}
	}

	PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Resulting from path: %" PRIu32 ":%" PRIu32 ":%" PRIu32,
			fromPath[0], fromPath[1], fromPath[2]);
	PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Resulting to   path: %" PRIu32 ":%" PRIu32 ":%" PRIu32,
			toPath[0], toPath[1], toPath[2]);



	if(fromPath[0] == 0 && toPath[0] >= addrsPerPage - 1){
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Delete spans over complete depth 0");
		anchor = 0;
		//just shut everythin below this
		start[0].active = false;
		return Result::ok;
	}

	//TODO: Immediately invalidate all pages (data as well as index)
	/**
	if(!start[0].active){
		r = loadCacheElem(anchor, start[0]);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read elem 0 in first depth!");
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
	}**/
	return Result::nimpl;
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
	PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Committing CacheElem referenced by "
			"parent:%" PRIu32, elem.positionInParent);
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
	if(from == 0 || from == combineAddress(0, unusedMarker)){
		//This data was not used yet
		PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "load empty CacheElem (new)");
		memset(list, 0, addrsPerPage * sizeof(Addr));
		return Result::ok;
	}
	if(dev->areaMap[extractLogicalArea(from)].type != AreaType::index){
		PAFFS_DBG(PAFFS_TRACE_BUG, "READ ADDR LIST operation of invalid area at %d:%d",\
				extractLogicalArea(from),
				extractPage(from));
		return Result::bug;
	}
	PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "loadCacheElem from %"
			PRIu32 ":%" PRIu32, extractLogicalArea(from), extractPage(from));
	Result res = dev->driver->readPage(getPageNumber(from, dev),list, addrsPerPage * sizeof(Addr));
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
	dev->activeArea[AreaType::index] = dev->areaMgmt.findWritableArea(AreaType::index);
	if(dev->lasterr != Result::ok){
		//TODO: Reset former pagestatus, so that FS will be in a safe state
		return dev->lasterr;
	}
	if(dev->activeArea[AreaType::index] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "findWritableArea returned 0");
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
			dev->activeArea[AreaType::index]) == Result::nosp){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::index]);
		return dev->lasterr = Result::bug;
	}
	to = combineAddress(dev->activeArea[AreaType::index], firstFreePage);

	//Mark Page as used
	r = dev->sumCache.setPageStatus(dev->activeArea[AreaType::index],
			firstFreePage, SummaryEntry::used);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not mark Page as used!");
		return r;
	}

	r = dev->areaMgmt.manageActiveAreaFull(&dev->activeArea[AreaType::index], AreaType::index);
	if(r != Result::ok)
		return r;

	r = dev->driver->writePage(getPageNumber(to, dev), reinterpret_cast<char*>(list),
			addrsPerPage * sizeof(Addr));
	if(r != Result::ok){
		//TODO: Revert Changes to PageStatus
		return r;
	}

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

