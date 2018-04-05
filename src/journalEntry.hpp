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
#include "bitlist.hpp"

namespace paffs
{
struct JournalEntry
{
    enum Topic : uint8_t
    {
        invalid = 0,
        checkpoint,
        pagestate,
        superblock,
        areaMgmt,
        garbage,
        summaryCache,
        tree,
        dataIO, //DataIO before PAC to invalidate pages
        pac,
        device,
    };

    static constexpr const unsigned char numberOfTopics = 11;
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
        enum class Type : uint8_t
        {
            replacePage,
            replacePagePos,
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

        struct ReplacePagePos : public Pagestate
        {
            Addr    neu;
            Addr    old;
            InodeNo nod;
            PageAbs pos;
            inline
            ReplacePagePos(Topic _target, Addr _new, Addr _old, InodeNo _nod, Addr _pos) :
                Pagestate(_target, Type::replacePagePos), neu(_new), old(_old), nod(_nod), pos(_pos){};
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
            ReplacePagePos replacePagePos;
            Success success;
            InvalidateOldPages invalidateOldPages;
        };
    }

    struct Superblock : public JournalEntry
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
        Superblock(Type _type) : JournalEntry(Topic::superblock), type(_type){};
    };

    namespace superblock
    {
        struct Rootnode : public Superblock
        {
            Addr addr;
            Rootnode(Addr _addr) : Superblock(Type::rootnode), addr(_addr){};
        };

        struct AreaMap : public Superblock
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
                Swap swap;
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
            ActiveArea activeArea;
            UsedAreas usedAreas;
        };
    };

    struct AreaMgmt : public JournalEntry
    {
        enum class Operation : uint8_t
        {
            initAreaAs,
            closeArea,
            retireArea,
            deleteAreaContents,
            deleteArea,
        };
        AreaPos area;
        Operation operation;
    protected:
        inline
        AreaMgmt(AreaPos _area, Operation _operation) : JournalEntry(Topic::areaMgmt),
            area(_area), operation(_operation){};
    };

    namespace areaMgmt
    {
        struct InitAreaAs : public AreaMgmt
        {
            AreaType type;
            inline
            InitAreaAs(AreaPos _area, AreaType _type) : AreaMgmt(_area, Operation::initAreaAs),
                    type(_type){};
        };
        struct CloseArea : public AreaMgmt
        {
            inline
            CloseArea(AreaPos _area) : AreaMgmt(_area, Operation::closeArea){};
        };
        struct RetireArea : public AreaMgmt
        {
            inline
            RetireArea(AreaPos _area) : AreaMgmt(_area, Operation::retireArea){};
        };
        struct DeleteAreaContents : public AreaMgmt
        {
            AreaPos swappedArea;
            inline
            DeleteAreaContents(AreaPos target, AreaPos _swappedArea) :
                AreaMgmt(target, Operation::deleteAreaContents), swappedArea(_swappedArea){};
        };
        struct DeleteArea : public AreaMgmt
        {
            inline
            DeleteArea(AreaPos target) : AreaMgmt(target, Operation::deleteArea){};
        };

        union Max
        {
            AreaMgmt base;

            InitAreaAs initAreaAs;
            CloseArea closeArea;
            RetireArea retireArea;
            DeleteAreaContents deleteAreaContents;
            DeleteArea deleteArea;
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
    struct GarbageCollection : public JournalEntry
    {
        enum class Operation : uint8_t
        {
            moveValidData,
        };
        Operation operation;
    protected:
        inline
        GarbageCollection(Operation _operation) : JournalEntry(Topic::garbage), operation(_operation){};
    };

    namespace garbageCollection
    {
        struct MoveValidData : public GarbageCollection
        {
            AreaPos from;
            //always to Area 3 (GC)
            inline
            MoveValidData(AreaPos _from) : GarbageCollection(Operation::moveValidData),
            from(_from){};
        };

        union Max
        {
            MoveValidData moveValidData;
        };
    }

    struct SummaryCache : public JournalEntry
    {
        enum class Subtype : uint8_t
        {
            commit,
            remove,
            reset,
            setStatus,
            setStatusBlock,
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

        struct ResetASWritten : public SummaryCache
        {
            inline
            ResetASWritten(AreaPos _area) : SummaryCache(_area, Subtype::reset){};
        };

        struct SetStatus : public SummaryCache
        {
            PageOffs page;
            SummaryEntry status;
            inline SetStatus(AreaPos _area, PageOffs _page, SummaryEntry _status) :
                    SummaryCache(_area, Subtype::setStatus), page(_page), status(_status){};
        };

        struct SetStatusBlock : public SummaryCache
        {
            TwoBitList<dataPagesPerArea> status;
            inline SetStatusBlock(AreaPos _area, TwoBitList<dataPagesPerArea>& _status) :
                    SummaryCache(_area, Subtype::setStatusBlock), status(_status){};
        };

        union Max
        {
            Commit commit;
            Remove remove;
            SetStatus setStatus;
            SetStatusBlock setStatusBlock;
        };
    }


    struct BTree : public JournalEntry
    {
        enum class Operation : uint8_t
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

        union Max {
            Insert insert;
            Update update;
            Remove remove;
        };
    };

    //TODO: Skip "operation" to save space
    struct PAC : public JournalEntry
    {
    enum class Operation : uint8_t
    {
        setAddress,
    };
    Operation operation;

    protected:
        inline
        PAC(Operation _operation) :
            JournalEntry(Topic::pac), operation(_operation){};
    };

    namespace pac
    {
        struct SetAddress : public PAC
        {
            InodeNo inodeNo;
            PageOffs page;
            Addr addr;
            inline
            SetAddress(InodeNo _inodeno, PageOffs _page, Addr _addr) : PAC(Operation::setAddress),
            inodeNo(_inodeno), page(_page), addr(_addr){};
        };

        union Max {
            SetAddress setAddress;
        };
    }

    struct DataIO : public JournalEntry
    {
        enum class Operation : uint8_t
        {
            newInodeSize,
        } operation;
    protected:
        inline
        DataIO(Operation _operation) : JournalEntry(Topic::dataIO), operation(_operation){};
    };

    namespace dataIO
    {
        struct NewInodeSize : public DataIO
        {
            InodeNo inodeNo;
            FileSize filesize;
            inline
            NewInodeSize(InodeNo _inodeNo, FileSize _filesize) : DataIO(Operation::newInodeSize),
                         inodeNo(_inodeNo), filesize(_filesize){};
        };

        union Max
        {
            NewInodeSize newInodeSize;
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
            InodeNo dirInode;
        public:
            inline
            InsertIntoDir(InodeNo _dirInode) : Device(Action::insertIntoDir), dirInode(_dirInode){};
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
        JournalEntry base;

        Checkpoint checkpoint;
        Pagestate pagestate;
        pagestate::Max pagestate_;
        Superblock superblock;
        superblock::Max superblock_;
        AreaMgmt areaMgmt;
        areaMgmt::Max areaMgmt_;
        GarbageCollection garbage;
        garbageCollection::Max garbage_;
        BTree btree;
        btree::Max btree_;
        SummaryCache summaryCache;
        summaryCache::Max summaryCache_;
        PAC pac;
        pac::Max pac_;
        DataIO dataIO;
        dataIO::Max dataIO_;
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
