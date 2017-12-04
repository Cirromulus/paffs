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
#include "bitlist.hpp"
#include "commonTypes.hpp"
#include <map>
namespace paffs
{
template <typename T, size_t size>
struct ObjectPool
{
    BitList<size> activeObjects;
    T objects[size];
    inline
    ~ObjectPool()
    {
        clear();
    }
    inline Result
    getNewObject(T*& outObj)
    {
        size_t objOffs = activeObjects.findFirstFree();
        if (objOffs >= size)
        {
            return Result::nospace;
        }
        activeObjects.setBit(objOffs);
        new (&objects[objOffs]) T;
        outObj = &objects[objOffs];
        return Result::ok;
    }
    inline Result
    freeObject(T& obj)
    {
        if (!isFromPool(obj))
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried freeing an Object out of range!");
            return Result::invalidInput;
        }
        if (!activeObjects.getBit(&obj - objects))
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried freeing an unused Object!");
            return Result::bug;
        }
        activeObjects.resetBit(&obj - objects);
        obj.~T();
        memset(&obj, 0, sizeof(T));
        return Result::ok;
    }
    inline bool
    isFromPool(T& obj)
    {
        return (&obj - objects >= 0 && static_cast<unsigned int>(&obj - objects) < size);
    }
    inline size_t
    getUsage()
    {
        uint8_t usedObjects = 0;
        for (unsigned int i = 0; i < size; i++)
        {
            if (activeObjects.getBit(i))
            {
                usedObjects++;
            }
        }
        return usedObjects;
    }

    inline void
    clear()
    {
        PAFFS_DBG_S(PAFFS_TRACE_BUFFERS, "Clear ObjectPool");
        for(size_t i(0); i < size; i++)
        {
            if(activeObjects.getBit(i))
            {
                PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Freeing forgotten Object");
                freeObject(objects[i]);
            }
        }
        activeObjects.clear();
    }
};

struct InodePoolBase
{
    typedef std::pair<Inode*, uint8_t> InodeWithRefcount;
    typedef std::map<InodeNo, InodeWithRefcount> InodeMap;
    typedef std::pair<InodeNo, InodeWithRefcount> InodeMapElem;

    virtual ~InodePoolBase(){};
    virtual Result
    getExistingInode(InodeNo no, SmartInodePtr& target) = 0;
    virtual Result
    removeInodeReference(InodeNo no) = 0;
};

template <size_t size>
struct InodePool : InodePoolBase
{
    ObjectPool<Inode, size> pool;
    InodeMap map;

    inline
    ~InodePool()
    {
        clear();
    }

    inline Result
    getExistingInode(InodeNo no, SmartInodePtr& target) override
    {
        InodeMap::iterator it = map.find(no);
        if (it == map.end())
        {
            return Result::notFound;
        }
        Inode* inode = it->second.first;  // Inode Pointer
        it->second.second++;              // Inode Refcount
        PAFFS_DBG_S(PAFFS_TRACE_BUFFERS, "Increased Refcount of %" PTYPE_INODENO " to %" PRIu8 "", no, it->second.second);
        target.setInode(*inode, *this);
        return Result::ok;
    }
    inline Result
    requireNewInode(InodeNo no, SmartInodePtr& target)
    {
        InodeMap::iterator it = map.find(no);
        if (it != map.end())
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried adding existing inode to Pool");
            return Result::bug;
        }
        Inode* inode;
        // Not yet opened
        Result r = pool.getNewObject(inode);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not get new Inode from Pool");
            return r;
        }
        memset(inode, 0, sizeof(Inode));
        inode->no = no;
        target.setInode(*inode, *this);
        map.insert(InodeMapElem(no, InodeWithRefcount(target, 1)));
        //DEBUG
        PAFFS_DBG_S(PAFFS_TRACE_BUFFERS, "Added new Inode %" PRIu32 "", no);
        return Result::ok;
    }
    inline Result
    removeInodeReference(InodeNo no) override
    {
        InodeMap::iterator it = map.find(no);
        if (it == map.end())
        {
            //Upon unmount, we dont care about that
            //PAFFS_DBG(PAFFS_TRACE_BUG, "inode %" PRIu32 " was not found in pool!", no);
            return Result::bug;
        }
        if (it->second.second == 0)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Obj's Refcount is already zero!");
            return Result::bug;
        }
        it->second.second--;  // Inode refcount
        PAFFS_DBG_S(PAFFS_TRACE_BUFFERS, "Decreased Refcount of %" PTYPE_INODENO " to %" PRIu8 "", no, it->second.second);
        if (it->second.second == 0)
        {
            PAFFS_DBG_S(PAFFS_TRACE_BUFFERS, "Freeing Inode %" PRIu32 "", it->first);
            Result r = pool.freeObject(*it->second.first);  // Inode
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free Inode from Pool in Openlist!");
            }
            map.erase(it);
        }

        return Result::ok;
    }
    inline Result
    removeInode(InodeNo no)
    {
        InodeMap::iterator it = map.find(no);
        if (it == map.end())
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "inode %" PRIu32 " was not found in pool!", no);
            return Result::bug;
        }
        if (it->second.second == 0)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Obj's Refcount is already zero!");
            return Result::bug;
        }
        Result r = pool.freeObject(*it->second.first);  // Inode
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free Inode from Pool in Openlist!");
        }
        PAFFS_DBG_S(PAFFS_TRACE_BUFFERS, "Force-Freeing Inode %" PRIu32 "", it->first);
        map.erase(it);
        return Result::ok;
    }

    inline void
    clear()
    {
        PAFFS_DBG_S(PAFFS_TRACE_BUFFERS, "Clear InodePool");
        InodeMap::iterator it = map.begin();
        if(it != map.end())
        {
            PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Freeing forgotten Inode %" PRIu32, it->first);
            while(it != map.end())
            {
                InodeMap::iterator old = it;
                it++;
                removeInode(old->first);
            }
        }
        pool.clear();
        map.clear();
    }

    inline size_t
    getUsage()
    {
        return pool.getUsage();
    }
};
};
