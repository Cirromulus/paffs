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

namespace paffs
{
struct JournalEntry
{
    enum Topic : uint8_t
    {
        invalid = 0,
        checkpoint,
        pagestate,
        areaMgmt,
        tree,
        summaryCache,
        pac,
        dataIO,
        device,
    };

    static constexpr const unsigned char numberOfTopics = 8;
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

    struct Pagestate : public JournalEntry
    {
        enum Type : uint8_t
        {
            replacePage,
            success,    ///> Warn: This is not to be written into journal, only interpreted by topic from other actions
            invalidateOldPages,
        };
        Topic target;
        Type type;
    protected:
        inline
        Pagestate(Topic _target, Type _type) : JournalEntry(Topic::pagestate),
        target(_target), type(_type){};
    };

    namespace pagestate
    {
        struct ReplacePage : public Pagestate
        {
            Addr neu;
            Addr old;
            inline
            ReplacePage(Topic _target, Addr _new, Addr _old) :
                Pagestate(_target, Type::replacePage), neu(_new), old(_old){};
        };

        struct Success : public Pagestate
        {
            inline
            Success(Topic _target) : Pagestate(_target, Type::success){};
        };
        struct InvalidateOldPages : public Pagestate
        {
            inline
            InvalidateOldPages(Topic _target) : Pagestate(_target, Type::invalidateOldPages){};
        };

        union Max
        {
            ReplacePage replacePage;
            Success success;
            InvalidateOldPages invalidateOldPages;
        };
    }

    struct AreaMgmt : public JournalEntry
    {
        enum Type : uint8_t
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
            Addr addr;
            Rootnode(Addr _addr) : AreaMgmt(Type::rootnode), addr(_addr){};
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
                Swap swap;
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
            Rootnode rootnode;
            AreaMap areaMap;
            areaMap::Max areaMap_;
            ActiveArea activeArea;
            UsedAreas usedAreas;
        };
    };

    struct BTree : public JournalEntry
    {
        enum class Operation : uint8_t
        {
            insert,
            update,
            remove,
            setRootnode,
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

        struct SetRootnode : public BTree
        {
            Addr address;
        protected:
            SetRootnode(Addr _address) : BTree(Operation::setRootnode), address(_address){};
        };

        union Max {
            Insert insert;
            Update update;
            Remove remove;
            SetRootnode setRootnode;
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
            SetStatus setStatus;
        };
    }

    struct PAC : public JournalEntry
    {
    enum class Operation : uint8_t
    {
        setInode,
        setAddress,
        updateAddresslist,
    };
    Operation operation;

    protected:
        inline
        PAC(Operation _operation) :
            JournalEntry(Topic::pac), operation(_operation){};
    };

    namespace pac
    {
        struct SetInode : public PAC
        {
            InodeNo inodeNo;
            inline
            SetInode(InodeNo _inodeNo) : PAC(Operation::setInode), inodeNo(_inodeNo){};
        };

        struct SetAddress : public PAC
        {
            PageOffs page;
            Addr addr;
            inline
            SetAddress(PageOffs _page, Addr _addr) : PAC(Operation::setAddress), page(_page), addr(_addr){};
        };

        struct UpdateAddressList : public PAC
        {
            //TODO: Build a system that allows some log entries to be received by multiple Topics
            //This would solve many doubled messages (including this one)
            Inode inode;
            inline
            UpdateAddressList(Inode _inode) : PAC(Operation::updateAddresslist), inode(_inode){};
        };

        union Max {
            SetInode setInode;
            SetAddress setAddress;
            UpdateAddressList updateAddressList;
        };
    }

    struct DataIO : public JournalEntry
    {

    };

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
        Pagestate pagestate;
        pagestate::Max pagestate_;
        AreaMgmt areaMgmt;
        areaMgmt::Max areaMgmt_;
        BTree btree;
        btree::Max btree_;
        SummaryCache summaryCache;
        summaryCache::Max summaryCache_;
        PAC pac;
        pac::Max pac_;
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
