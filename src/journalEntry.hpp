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
    enum Topic : uint8_t
    {
        invalid = 0,
        checkpoint,
        areaMgmt,
        tree,
        summaryCache,
        pac,
        device,
    };

    static constexpr const unsigned char numberOfTopics = 7;
    Topic topic;

protected:
    inline
    JournalEntry(Topic _topic) : topic(_topic){};
};

namespace journalEntry
{
    struct Checkpoint : public JournalEntry
    {
        // Target should only be Superblock and Tree.
        Topic target;
        inline
        Checkpoint(Topic _target) : JournalEntry(Topic::checkpoint), target(_target){};
    };

    struct AreaMgmt : public JournalEntry
    {
        enum class Type : uint8_t
        {
            rootnode,
            areaMap,
            activeArea,
            usedAreas,
        };
        Type type;

    protected:
        inline
        AreaMgmt(Type _type) : JournalEntry(Topic::areaMgmt), type(_type){};
    };

    namespace areaMgmt
    {
        struct Rootnode : public AreaMgmt
        {
            Addr rootnode;
            Rootnode(Addr _rootnode) : AreaMgmt(Type::rootnode), rootnode(_rootnode){};
        };

        struct AreaMap : public AreaMgmt
        {
            enum class Operation : uint8_t
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
                AreaMgmt(AreaMgmt::Type::areaMap), offs(_offs), operation(_operation){};
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

        struct ActiveArea : public AreaMgmt
        {
            AreaType type;
            AreaPos area;
            inline
            ActiveArea(AreaType _type, AreaPos _area) :
                AreaMgmt(Type::activeArea), type(_type), area(_area){};
        };

        struct UsedAreas : public AreaMgmt
        {
            AreaPos usedAreas;
            inline
            UsedAreas(AreaPos _usedAreas) : AreaMgmt(Type::usedAreas), usedAreas(_usedAreas){};
        };

        union Max {
            AreaMap areaMap;
            areaMap::Max areaMap_;
        };
    };

    struct BTree : public JournalEntry
    {
        enum class Operation : uint8_t
        {
            insert,
            update,
            remove,
            commit,
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
            Inode inode;
            inline
            Insert(Inode _inode) : BTree(Operation::insert), inode(_inode){};
        };
        struct Update : public BTree
        {
            Inode inode;
            inline
            Update(Inode _inode) : BTree(Operation::update), inode(_inode){};
        };
        struct Remove : public BTree
        {
            InodeNo no;
            inline
            Remove(InodeNo _no) : BTree(Operation::remove), no(_no){};
        };

        struct Commit : public BTree
        {
            enum class Action : uint8_t
            {
                setNewPage,
                setOldPage,
                setRootnode,
                invalidateOld,
            };
            Action action;
            Addr address;
        protected:
            Commit(Action _action, Addr _address) : BTree(Operation::commit),
                    action(_action), address(_address){};
        };

        namespace commit
        {
            struct SetNewPage : public Commit
            {
                inline
                SetNewPage(Addr newPage) : Commit(Action::setNewPage, newPage){};
            };

            struct SetOldPage : public Commit
            {
                inline
                SetOldPage(Addr oldPage) : Commit(Action::setOldPage, oldPage){};
            };

            struct SetRootnode : public Commit
            {
                inline
                SetRootnode(Addr rootnode) : Commit(Action::setRootnode, rootnode){};
            };

            struct InvalidateOld : public Commit
            {
                inline
                InvalidateOld() : Commit(Action::invalidateOld, 0){};
            };

            union Max {
                SetNewPage setNewPage;
                SetOldPage setOldPage;
                SetRootnode setRootnode;
                InvalidateOld invalidateOld;
            };
        }

        union Max {
            Insert insert;
            Update update;
            Remove remove;
            Commit commit;
            commit::Max commit_;
        };
    };

    struct SummaryCache : public JournalEntry
    {
        enum class Subtype : uint8_t
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
            inline SetStatus(AreaPos _area, PageOffs _page, SummaryEntry _status) :
                    SummaryCache(_area, Subtype::setStatus), page(_page), status(_status){};
        };

        union Max
        {
            Commit commit;
            Remove remove;
        };
    }

    struct PAC : public JournalEntry
    {
    enum class Operation : uint8_t
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
        PAC(Operation _operation, InodeNo _inode) :
            JournalEntry(Topic::pac), operation(_operation), inode(_inode){};
    };

    namespace pac
    {
        // TODO
        struct Add : public PAC
        {
            inline
            Add(InodeNo _inode) : PAC(Operation::add, _inode){};
        };

        struct Write : public PAC
        {
            inline
            Write(InodeNo _inode) : PAC(Operation::write, _inode){};
        };

        struct Remove : public PAC
        {
            inline
            Remove(InodeNo _inode) : PAC(Operation::remove, _inode){};
        };
        struct Commit : public PAC
        {
            inline
            Commit(InodeNo _inode) : PAC(Operation::commit, _inode){};
        };

        union Max {
            Add add;
            Write write;
            Remove remove;
            Commit commit;
        };
    }

    struct Device : public JournalEntry
    {
        enum Action : uint8_t
        {
            mkObjInode,
            insertIntoDir,
            removeObj,
        };

        Action action;
    protected:
        inline
        Device(Action _action) :
            JournalEntry(Topic::device), action(_action){};
    };

    namespace device
    {
        struct MkObjInode : public Device
        {
            InodeNo inode;
        public:
            inline
            MkObjInode(InodeNo _inode) : Device(Action::mkObjInode), inode(_inode){};
        };

        struct InsertIntoDir : public Device
        {
            InodeNo inode;
        public:
            inline
            InsertIntoDir(InodeNo _inode) : Device(Action::insertIntoDir), inode(_inode){};
        };

        struct RemoveObj : public Device
        {
            InodeNo obj;
            InodeNo parDir;
        public:
            inline
            RemoveObj(InodeNo _obj, InodeNo _parDir) : Device(Action::removeObj),
                obj(_obj), parDir(_parDir){};
        };


        union Max
        {
            MkObjInode mkObjInode;
            InsertIntoDir insertIntoDir;
            RemoveObj removeObj;

        };
    }

    union Max {
        JournalEntry base;  // Not nice?

        Checkpoint checkpoint;
        AreaMgmt superblock;
        areaMgmt::Max superblock_;
        BTree btree;
        btree::Max btree_;
        SummaryCache summaryCache;
        summaryCache::Max summaryCache_;
        PAC inode;
        pac::Max inode_;
        Device device;
        device::Max device_;
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
