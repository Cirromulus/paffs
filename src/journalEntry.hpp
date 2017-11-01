/*
 * journalEntry.hpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

#pragma once
#include "commonTypes.hpp"
#include <type_traits>

namespace paffs
{

template <typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) noexcept {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

struct JournalEntry{
	enum class Topic{
		checkpoint = 1,
		success,
		superblock,
		tree,
		summaryCache,
		inode,
	};
	static constexpr const char* topicNames[] = {
		"CHECKPOINT",
		"SUCCEED",
		"SUPERBLOCK",
		"TREE",
		"SUMMARY CACHE",
		"INODE",
	};
	static constexpr const unsigned char numberOfTopics = 6;
	Topic topic;
protected:
	JournalEntry(Topic _topic) : topic(_topic){};
public:
	virtual ~JournalEntry(){};
};

namespace journalEntry
{
	struct Checkpoint : public JournalEntry
	{
		Checkpoint() : JournalEntry(Topic::checkpoint){};
	};

	struct Success : public JournalEntry
	{
		//Target should only be Superblock and Tree.
		Topic target;
		Success(Topic _target) : JournalEntry(Topic::success), target(_target){};
	};

	struct Superblock : public JournalEntry{
		enum class Type{
			rootnode,
			areaMap,
			activeArea,
			usedAreas,
		};
		Type type;
	protected:
		Superblock(Type _type) : JournalEntry(Topic::superblock),
				type(_type){};
	public:
		virtual ~Superblock(){};
	};

	namespace superblock{

		struct Rootnode : public Superblock
		{
			Addr rootnode;
			Rootnode(Addr _rootnode) : Superblock(Type::rootnode),
					rootnode(_rootnode){};
		};

		struct AreaMap : public Superblock{
			enum class Operation{
				type,
				status,
				increaseErasecount,
				position,
				swap,
			};
			AreaPos offs;
			Operation operation;
		protected:
			AreaMap(AreaPos _offs, Operation _operation) : Superblock(Superblock::Type::areaMap),
					offs(_offs), operation(_operation){};
		public:
			virtual ~AreaMap(){};
		};

		namespace areaMap
		{
			struct Type : public AreaMap{
				Type(AreaPos _offs, AreaType _type) : AreaMap(_offs, Operation::type),
						type(_type){};
				AreaType type;
			};
			struct Status : public AreaMap{
				Status(AreaPos _offs, AreaStatus _status) : AreaMap(_offs, Operation::status),
						status(_status){};
				AreaStatus status;
			};
			struct IncreaseErasecount : public AreaMap{
				IncreaseErasecount(AreaPos _offs) : AreaMap(_offs, Operation::increaseErasecount){};
			};
			struct Position : public AreaMap{
				Position(AreaPos _offs, AreaPos _position) : AreaMap(_offs, Operation::position),
						position(_position){};
				AreaPos position;
			};
			struct Swap : public AreaMap{
				AreaPos b;
				Swap(AreaPos _a, AreaPos _b) : AreaMap(_a, Operation::swap),
						b(_b){};
			};
			union Max
			{
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
			ActiveArea(AreaType _type, AreaPos _area) : Superblock(Type::activeArea),
					type(_type), area(_area){};
		};

		struct UsedAreas : public Superblock
		{
			AreaPos usedAreas;
			UsedAreas(AreaPos _usedAreas) : Superblock(Type::usedAreas),
					usedAreas(_usedAreas){};
		};

		union Max
		{
			Rootnode rootnode;
			AreaMap areaMap;
			areaMap::Max areaMap_;
		};
	};

	struct BTree : public JournalEntry{
		enum class Operation{
			insert,
			update,
			remove,
		};
		Operation op;
	protected:
		BTree(Operation _operation) : JournalEntry(Topic::tree),
				op(_operation){};
	public:
		~BTree(){};
	};

	namespace btree{
		struct Insert : public BTree{
			paffs::Inode inode;
			Insert(Inode _inode): BTree(Operation::insert), inode(_inode){};
		};
		struct Update : public BTree{
			paffs::Inode inode;
			Update(Inode _inode): BTree(Operation::update), inode(_inode){};
		};
		struct Remove : public BTree{
			InodeNo no;
			Remove(InodeNo _no): BTree(Operation::remove), no(_no){};
		};

		union Max
		{
			Insert insert;
			Update update;
			Remove remove;
		};
	};

	struct SummaryCache : public JournalEntry{
		enum class Subtype{
			commit,
			remove,
			setStatus,
		};
		AreaPos area;
		Subtype subtype;
	protected:
		SummaryCache(AreaPos _area, Subtype _subtype) : JournalEntry(Topic::summaryCache),
					area(_area), subtype(_subtype){};
	public:
		~SummaryCache(){};
	};

	namespace summaryCache{
		struct Commit : public SummaryCache{
			Commit(AreaPos _area) : SummaryCache(_area, Subtype::commit){};
		};

		struct Remove : public SummaryCache{
			Remove(AreaPos _area) : SummaryCache(_area, Subtype::remove){};
		};

		struct SetStatus : public SummaryCache{
			PageOffs page;
			SummaryEntry status;
			SetStatus(AreaPos _area, PageOffs _page, SummaryEntry _status) :
				SummaryCache(_area, Subtype::setStatus), page(_page), status(_status){};
		};

		union Max
		{
			Commit commit;
			SetStatus setStatus;
		};
	}

	struct Inode : public JournalEntry{
		enum class Operation{
			add,
			write,
			remove,
			commit
		};
		Operation operation;
		InodeNo inode;
	protected:
		Inode(Operation _operation, InodeNo _inode) : JournalEntry(Topic::inode),
			operation(_operation), inode(_inode){};
	public:
		virtual ~Inode(){};
	};

	namespace inode
	{
		//TODO
		struct Add : public Inode
		{
			Add(InodeNo _inode) : Inode(Operation::add, _inode){};
		};

		struct Write : public Inode
		{
			Write(InodeNo _inode) : Inode(Operation::write, _inode){};
		};

		struct Remove : public Inode
		{
			Remove(InodeNo _inode) : Inode(Operation::remove, _inode){};
		};
		struct Commit : public Inode
		{
			Commit(InodeNo _inode) : Inode(Operation::commit, _inode){};
		};


		union Max
		{
			Add add;
			Write write;
			Remove remove;
			Commit commit;
		};
	}
	union Max
	{
		JournalEntry      base;		//Not nice?

		Checkpoint        checkpoint;
		Success           success;
		Superblock        superblock;
		superblock::Max   superblock_;
		BTree             btree;
		btree::Max        btree_;
		SummaryCache      summaryCache;
		summaryCache::Max summaryCache_;
		Inode             inode;
		inode::Max        inode_;
		Max()
		{
			memset(static_cast<void*>(this), 0, sizeof(Max));
		};
		~Max(){};
		Max(const Max &other)
		{
			memcpy(static_cast<void*>(this), static_cast<const void*>(&other), sizeof(Max));
		}
	};
}
}
