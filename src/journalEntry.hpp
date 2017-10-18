/*
 * journalEntry.hpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

#pragma once
#include "commonTypes.hpp"
#include <iostream>

namespace paffs
{

struct JournalEntry{
	enum class Topic{
		empty,
		transaction,
		superblock,
		tree,
		summaryCache,
		inode,
	};
	static constexpr const char* topicNames[] = {
		"EMPTY",
		"TRANSACTON",
		"SUPERBLOCK",
		"TREE",
		"SUMMARY CACHE",
		"INODE",
	};
	Topic topic;
protected:
	JournalEntry(Topic _topic) : topic(_topic){};
public:
	virtual ~JournalEntry(){};
};

namespace journalEntry
{
	struct Transaction : public JournalEntry
	{
		enum class Status
		{
			checkpoint,
			success,
		};
		static constexpr const char* statusNames[] = {
			"CHECKPOINT",
			"SUCCESS",
		};
		Topic target;
		Status status;
		Transaction(Topic _target, Status _status) : JournalEntry(Topic::transaction),
					target(_target), status(_status){};
	};

	struct Superblock : public JournalEntry{
		enum class Subtype{
			rootnode,
			areaMap,
		};
		Subtype subtype;
	protected:
		Superblock(Subtype _subtype) : JournalEntry(Topic::superblock),
				subtype(_subtype){};
	public:
		virtual ~Superblock(){};
	};

	namespace superblock{

		struct Rootnode : public Superblock{
			Rootnode(Addr _rootnode) : Superblock(Subtype::rootnode),
					rootnode(_rootnode){};
			Addr rootnode;
		};

		struct AreaMap : public Superblock{
			enum class Element{
				type,
				status,
				erasecount,
				position,
				swap,
			};
			AreaPos offs;
			Element element;
		protected:
			AreaMap(AreaPos _offs, Element _element) : Superblock(Superblock::Subtype::areaMap),
					offs(_offs), element(_element){};
		public:
			virtual ~AreaMap(){};
		};

		namespace areaMap
		{
			struct Type : public AreaMap{
				Type(AreaPos _offs, AreaType _type) : AreaMap(_offs, Element::type),
						type(_type){};
				AreaType type;
			};
			struct Status : public AreaMap{
				Status(AreaPos _offs, AreaStatus _status) : AreaMap(_offs, Element::status),
						status(_status){};
				AreaStatus status;
			};
			struct Erasecount : public AreaMap{
				Erasecount(AreaPos _offs, uint32_t _erasecount) : AreaMap(_offs, Element::erasecount),
						erasecount(_erasecount){};
				uint32_t erasecount;
			};
			struct Position : public AreaMap{
				Position(AreaPos _offs, AreaPos _position) : AreaMap(_offs, Element::position),
						position(_position){};
				AreaPos position;
			};
			struct Swap : public AreaMap{
				AreaPos b;
				Swap(AreaPos _a, AreaPos _b) : AreaMap(_a, Element::swap),
						b(_b){};
			};
			union Max
			{
				Type type;
				Status status;
				Erasecount erasecount;
				Position position;
			};
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
		enum class Subtype{
			add,
			write,
			remove
		};
		Subtype subtype;
		InodeNo inode;
	protected:
		Inode(Subtype _subtype, InodeNo _inode) : JournalEntry(Topic::inode),
			subtype(_subtype), inode(_inode){};
	};

	namespace inode
	{
		//TODO
		struct Add : public Inode
		{
			Add(InodeNo _inode) : Inode(Subtype::add, _inode){};
		};

		struct Write : public Inode
		{
			Write(InodeNo _inode) : Inode(Subtype::write, _inode){};
		};

		struct Remove : public Inode
		{
			Remove(InodeNo _inode) : Inode(Subtype::remove, _inode){};
		};

		union Max
		{
			Inode inode;

			Add add;
			//TODO: Rest
		};
	}
	union Max
	{
		JournalEntry base;		//Not nice?

		Transaction transaction;
		Superblock superblock;
		superblock::Max superblock_;
		BTree btree;
		btree::Max btree_;
		SummaryCache summaryCache;
		summaryCache::Max summaryCache_;
		Inode inode;
		inode::Max inode_;
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
