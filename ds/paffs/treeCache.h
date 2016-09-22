/*
 * treeCache.h
 *
 *  Created on: 21.09.2016
 *      Author: rooot
 */

#ifndef TREECACHE_H_
#define TREECACHE_H_

#include "paffs.h"


PAFFS_RESULT getRootNodeFromCache(p_dev* dev, treeCacheNode* tcn);	//parent to self

PAFFS_RESULT getTreeNodeAtIndexFrom(p_dev* dev, unsigned char index, treeCacheNode* parent, treeCacheNode* child);

PAFFS_RESULT addNewCacheNode(p_dev* dev, treeCacheNode* newTcn);

#endif /* TREECACHE_H_ */
