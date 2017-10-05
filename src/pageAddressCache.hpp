/*
 * pageAddressCache.hpp
 *
 *  Created on: 17.06.2017
 *      Author: urinator
 */

#pragma once

#include "commonTypes.hpp"
#include "bitlist.hpp"

#include <functional>

namespace paffs{

typedef uint32_t PageNo;

struct AddrListCacheElem{
	Addr     cache[addrsPerPage];
	uint16_t positionInParent;
	bool     dirty:1;
	bool     active:1;
	AddrListCacheElem() : positionInParent(0), dirty(false), active(false){};
	void setAddr(PageNo pos, Addr addr);
	Addr getAddr(PageNo pos);
};

class PageAddressCache{
	AddrListCacheElem tripl[3];
	AddrListCacheElem doubl[2];	//name clash with double
	AddrListCacheElem singl;
	Device *dev;
	Inode *inode;
	typedef std::function<void(Addr)> InformJournalFunc;
public:
	PageAddressCache(Device *mdev) : dev(mdev), inode(nullptr){};
	Result setTargetInode(Inode* node);
	Result getPage(PageNo page, Addr *addr);
	Result setPage(PageNo page, Addr  addr);
	Result commit();
	bool isDirty();
private:
	uint16_t getCacheID(AddrListCacheElem* elem);
	void informJournal(uint16_t cacheID, const PageNo pos, const Addr newAddr);
	/**
	 * @param target outputs the address of the wanted page
	 */
	Result loadPath(Addr& anchor, PageNo pageOffs, AddrListCacheElem* start,
			unsigned char depth, PageNo &addrPos);

	Result commitPath(Addr& anchor, AddrListCacheElem* path, unsigned char depth);
	Result commitElem(AddrListCacheElem &parent, AddrListCacheElem &elem);
	Result loadCacheElem(Addr from, AddrListCacheElem &elem);
	Result writeCacheElem(Addr &source, AddrListCacheElem &elem);

	Result readAddrList (Addr from, Addr list[addrsPerPage]);
	/**
	 * @param to should contain former position of page
	 */
	Result writeAddrList(Addr &source, Addr list[addrsPerPage]);
	bool isAddrListPlausible(Addr* addrList, size_t elems);
};

}
