/*
 * treeCache.h
 *
 *  Created on: 21.09.2016
 *      Author: rooot
 */

#ifndef TREECACHE_H_
#define TREECACHE_H_

#include "paffs.h"
#include "btree.h"


PAFFS_RESULT getRootNodeFromCache(p_dev* dev, treeCacheNode** tcn);

PAFFS_RESULT getTreeNodeAtIndexFrom(p_dev* dev, unsigned char index,
									treeCacheNode* parent, treeCacheNode** child);

PAFFS_RESULT addNewCacheNode(p_dev* dev, treeCacheNode** newTcn);

PAFFS_RESULT removeCacheNode(p_dev* dev, treeCacheNode* tcn);

PAFFS_RESULT setCacheRoot(p_dev* dev, treeCacheNode* rootTcn);

PAFFS_RESULT flushTreeCache(p_dev* dev);


//debug
uint16_t getCacheUsage();
uint16_t getCacheSize();

#endif /* TREECACHE_H_ */
