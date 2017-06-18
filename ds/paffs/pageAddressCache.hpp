/*
 * pageAddressCache.hpp
 *
 *  Created on: 17.06.2017
 *      Author: urinator
 */

#pragma once

#include "commonTypes.hpp"
#include "bitlist.hpp"

namespace paffs{

typedef uint32_t PageNo;

struct AddrListCacheElem{
	Addr   cache[addrsPerPage];
	PageNo positionInParent:30;
	bool   dirty:1;
	bool   active:1;
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
public:
	PageAddressCache(Device *mdev) : dev(mdev), inode(nullptr){};
	Result setTargetInode(Inode* node);
	Result getPage(PageNo page, Addr *addr);
	Result setPage(PageNo page, Addr  addr);
	Result deletePage(PageNo pageFrom, PageNo pageTo);
	Result commit();
	bool isDirty();
private:
	/**
	 * @param target outputs the address of the wanted page
	 */
	Result loadPath(Addr& anchor, PageNo pageOffs, AddrListCacheElem* start,
			unsigned char depth, PageNo &addrPos);
	Result deletePath(Addr& anchor, PageNo pageFrom, PageNo pageTo,
			AddrListCacheElem* start, unsigned char depth);


	Result commitPath(Addr& anchor, AddrListCacheElem* path, unsigned char depth);
	Result commitElem(AddrListCacheElem &parent, AddrListCacheElem &elem);
	Result loadCacheElem(Addr from, AddrListCacheElem &elem);
	Result writeCacheElem(Addr &to, AddrListCacheElem &elem);

	Result readAddrList (Addr from, Addr list[addrsPerPage]);
	/**
	 * @param to should contain former position of page
	 */
	Result writeAddrList(Addr &to , Addr list[addrsPerPage]);
	bool isAddrListPlausible(Addr* addrList, size_t elems);
};

}
