/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#pragma once

namespace paffs{

struct InodePoolBase;
struct Inode;

class SmartInodePtr{
	Inode* mInode;
	InodePoolBase* mPool;
public:
	SmartInodePtr();
	SmartInodePtr(const SmartInodePtr &other);
	~SmartInodePtr();
	void setInode(Inode &inode, InodePoolBase &pool);
	Inode* operator->() const;
	operator Inode*() const;
	SmartInodePtr& operator=(const SmartInodePtr &other);
};

};
