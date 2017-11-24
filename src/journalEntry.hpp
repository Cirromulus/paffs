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
#include "commonTypes.hpp"
#include <type_traits>

namespace paffs
{
template <typename E>
constexpr typename std::underlying_type<E>::type
toUnderlying(E e) noexcept
{
    return static_cast<typename std::underlying_type<E>::type>(e);
}

struct JournalEntry
{
    enum class Topic
    {
        checkpoint = 1,
        success,
        superblock,
        tree,
        summaryCache,
        inode,
    };
    static constexpr const char* topicNames[] = {
            "CHECKPOINT", "SUCCEED", "SUPERBLOCK", "TREE", "SUMMARY CACHE", "INODE",
    };
    static constexpr const unsigned char numberOfTopics = 6;
    Topic topic;

protected:
    inline
    JournalEntry(Topic _topic) : topic(_topic){};
};

namespace journalEntry
{
    struct Checkpoint : public JournalEntry
    {
        inline
        Checkpoint() : JournalEntry(Topic::checkpoint){};
    };

    struct Success : public JournalEntry
    {
        // Target should only be Superblock and Tree.
        Topic target;
        inline
        Success(Topic _target) : JournalEntry(Topic::success), target(_target){};
    };

    struct Superblock : public JournalEntry
    {
        enum class Type
        {
            rootnode,
            areaMap,
            activeArea,
            usedAreas,
        };
        Type type;

    protected:
        inline
        Superblock(Type _type) : JournalEntry(Topic::superblock), type(_type){};
    };

    namespace superblock
    {
        struct Rootnode : public Superblock
        {
            Addr rootnode;
            inline
            Rootnode(Addr _rootnode) : Superblock(Type::rootnode), rootnode(_rootnode){};
        };

        struct AreaMap : public Superblock
        {
            enum class Operation
            {
                type,
                status,
                increaseErasecount,
                position,
                swap,
            };
            AreaPos offs;
            Operation operation;

        protected:
            inline
            AreaMap(AreaPos _offs, Operation _operation) :
                Superblock(Superblock::Type::areaMap), offs(_offs), operation(_operation){};
        };

        namespace areaMap
        {
            struct Type : public AreaMap
            {
                inline
                Type(AreaPos _offs, AreaType _type) : AreaMap(_offs, Operation::type), type(_type){};
                AreaType type;
            };
            struct Status : public AreaMap
            {
                inline
                Status(AreaPos _offs, AreaStatus _status) :
                    AreaMap(_offs, Operation::status), status(_status){};
                AreaStatus status;
            };
            struct IncreaseErasecount : public AreaMap
            {
                inline
                IncreaseErasecount(AreaPos _offs) : AreaMap(_offs, Operation::increaseErasecount){};
            };
            struct Position : public AreaMap
            {
                AreaPos position;
                inline
                Position(AreaPos _offs, AreaPos _position) :
                    AreaMap(_offs, Operation::position), position(_position){};
            };
            struct Swap : public AreaMap
            {
                AreaPos b;
                inline
                Swap(AreaPos _a, AreaPos _b) : AreaMap(_a, Operation::swap), b(_b){};
            };
            union Max {
                Type type;
                Status status;
                IncreaseErasecount erasecount;
                Position position;
            };
        };

        struct ActiveArea : public Superblock
        {
            AreaType type;
            AreaPos area;
            inline
            ActiveArea(AreaType _type, AreaPos _area) :
                Superblock(Type::activeArea), type(_type), area(_area){};
        };

        struct UsedAreas : public Superblock
        {
            AreaPos usedAreas;
            inline
            UsedAreas(AreaPos _usedAreas) : Superblock(Type::usedAreas), usedAreas(_usedAreas){};
        };

        union Max {
            Rootnode rootnode;
            AreaMap areaMap;
            areaMap::Max areaMap_;
        };
    };

    struct BTree : public JournalEntry
    {
        enum class Operation
        {
            insert,
            update,
            remove,
        };
        Operation op;

    protected:
        inline
        BTree(Operation _operation) : JournalEntry(Topic::tree), op(_operation){};
    };

    namespace btree
    {
        struct Insert : public BTree
        {
            paffs::Inode inode;
            inline
            Insert(Inode _inode) : BTree(Operation::insert), inode(_inode){};
        };
        struct Update : public BTree
        {
            paffs::Inode inode;
            inline
            Update(Inode _inode) : BTree(Operation::update), inode(_inode){};
        };
        struct Remove : public BTree
        {
            InodeNo no;
            inline
            Remove(InodeNo _no) : BTree(Operation::remove), no(_no){};
        };

        union Max {
            Insert insert;
            Update update;
            Remove remove;
        };
    };

    struct SummaryCache : public JournalEntry
    {
        enum class Subtype
        {
            commit,
            remove,
            setStatus,
        };
        AreaPos area;
        Subtype subtype;

    protected:
        inline
        SummaryCache(AreaPos _area, Subtype _subtype) :
            JournalEntry(Topic::summaryCache), area(_area), subtype(_subtype){};
    };

    namespace summaryCache
    {
        struct Commit : public SummaryCache
        {
            inline
            Commit(AreaPos _area) : SummaryCache(_area, Subtype::commit){};
        };

        struct Remove : public SummaryCache
        {
            inline
            Remove(AreaPos _area) : SummaryCache(_area, Subtype::remove){};
        };

        struct SetStatus : public SummaryCache
        {
            PageOffs page;
            SummaryEntry status;
            inline
            SetStatus(AreaPos _area, PageOffs _page, SummaryEntry _status) :
                SummaryCache(_area, Subtype::setStatus), page(_page), status(_status){};
        };

        union Max
        {
            Commit commit;
            SetStatus setStatus;
        };
    }

    struct Inode : public JournalEntry
    {
    enum class Operation
    {
        add,
        write,
        remove,
        commit
    };
    Operation operation;
    InodeNo inode;

    protected:
        inline
        Inode(Operation _operation, InodeNo _inode) :
            JournalEntry(Topic::inode), operation(_operation), inode(_inode){};
    };

    namespace inode
    {
        // TODO
        struct Add : public Inode
        {
            inline
            Add(InodeNo _inode) : Inode(Operation::add, _inode){};
        };

        struct Write : public Inode
        {
            inline
            Write(InodeNo _inode) : Inode(Operation::write, _inode){};
        };

        struct Remove : public Inode
        {
            inline
            Remove(InodeNo _inode) : Inode(Operation::remove, _inode){};
        };
        struct Commit : public Inode
        {
            inline
            Commit(InodeNo _inode) : Inode(Operation::commit, _inode){};
        };

        union Max {
            Add add;
            Write write;
            Remove remove;
            Commit commit;
        };
    }
    union Max {
        JournalEntry base;  // Not nice?

        Checkpoint checkpoint;
        Success success;
        Superblock superblock;
        superblock::Max superblock_;
        BTree btree;
        btree::Max btree_;
        SummaryCache summaryCache;
        summaryCache::Max summaryCache_;
        Inode inode;
        inode::Max inode_;
        inline
        Max()
        {
            memset(static_cast<void*>(this), 0, sizeof(Max));
        };
        inline
        ~Max(){};
        inline
        Max(const Max& other)
        {
            memcpy(static_cast<void*>(this), static_cast<const void*>(&other), sizeof(Max));
        }
    };
}
}
