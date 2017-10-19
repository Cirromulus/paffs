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
	virtual PageAbs getSize()
	{
		return sizeof(JournalEntry);
	}
	virtual ~JournalEntry(){};
};

namespace journalEntry
{
	struct Empty : public JournalEntry
	{
		uint16_t size;
		Empty(uint16_t _size) : JournalEntry(Topic::empty), size(_size){};
		PageAbs getSize() override
		{
			return size;
		}
	};

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
		PageAbs getSize() override
		{
			return sizeof(Transaction);
		}
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
		virtual PageAbs getSize() override
		{
			return sizeof(Superblock);
		}
		virtual ~Superblock(){};
	};

	namespace superblock{

		struct Rootnode : public Superblock{
			Addr rootnode;
			Rootnode(Addr _rootnode) : Superblock(Subtype::rootnode),
					rootnode(_rootnode){};
			PageAbs getSize() override
			{
				return sizeof(Rootnode);
			}
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
			virtual PageAbs getSize() override
			{
				return sizeof(AreaMap);
			}
			virtual ~AreaMap(){};
		};

		namespace areaMap
		{
			struct Type : public AreaMap{
				Type(AreaPos _offs, AreaType _type) : AreaMap(_offs, Element::type),
						type(_type){};
				AreaType type;
				virtual PageAbs getSize() override
				{
					return sizeof(Type);
				}
			};
			struct Status : public AreaMap{
				Status(AreaPos _offs, AreaStatus _status) : AreaMap(_offs, Element::status),
						status(_status){};
				AreaStatus status;
				virtual PageAbs getSize() override
				{
					return sizeof(Status);
				}
			};
			struct Erasecount : public AreaMap{
				Erasecount(AreaPos _offs, uint32_t _erasecount) : AreaMap(_offs, Element::erasecount),
						erasecount(_erasecount){};
				uint32_t erasecount;
				virtual PageAbs getSize() override
				{
					return sizeof(Erasecount);
				}
			};
			struct Position : public AreaMap{
				Position(AreaPos _offs, AreaPos _position) : AreaMap(_offs, Element::position),
						position(_position){};
				AreaPos position;
				virtual PageAbs getSize() override
				{
					return sizeof(Position);
				}
			};
			struct Swap : public AreaMap{
				AreaPos b;
				Swap(AreaPos _a, AreaPos _b) : AreaMap(_a, Element::swap),
						b(_b){};
				virtual PageAbs getSize() override
				{
					return sizeof(Swap);
				}
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
			virtual PageAbs getSize() override
			{
				return sizeof(Insert);
			}
		};
		struct Update : public BTree{
			paffs::Inode inode;
			Update(Inode _inode): BTree(Operation::update), inode(_inode){};
			virtual PageAbs getSize() override
			{
				return sizeof(Update);
			}
		};
		struct Remove : public BTree{
			InodeNo no;
			Remove(InodeNo _no): BTree(Operation::remove), no(_no){};
			virtual PageAbs getSize() override
			{
				return sizeof(Remove);
			}
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
		virtual PageAbs getSize() override
		{
			return sizeof(SummaryCache);
		}
	};

	namespace summaryCache{
		struct Commit : public SummaryCache{
			Commit(AreaPos _area) : SummaryCache(_area, Subtype::commit){};
			virtual PageAbs getSize() override
			{
				return sizeof(Commit);
			}
		};

		struct Remove : public SummaryCache{
			Remove(AreaPos _area) : SummaryCache(_area, Subtype::remove){};
			virtual PageAbs getSize() override
			{
				return sizeof(Remove);
			}
		};

		struct SetStatus : public SummaryCache{
			PageOffs page;
			SummaryEntry status;
			SetStatus(AreaPos _area, PageOffs _page, SummaryEntry _status) :
				SummaryCache(_area, Subtype::setStatus), page(_page), status(_status){};
			virtual PageAbs getSize() override
			{
				return sizeof(SetStatus);
			}
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
	public:
		virtual ~Inode(){};
		virtual PageAbs getSize() override
		{
			return sizeof(Inode);
		}
	};

	namespace inode
	{
		//TODO
		struct Add : public Inode
		{
			Add(InodeNo _inode) : Inode(Subtype::add, _inode){};
			virtual PageAbs getSize() override
			{
				return sizeof(Add);
			}
		};

		struct Write : public Inode
		{
			Write(InodeNo _inode) : Inode(Subtype::write, _inode){};
			virtual PageAbs getSize() override
			{
				return sizeof(Write);
			}
		};

		struct Remove : public Inode
		{
			Remove(InodeNo _inode) : Inode(Subtype::remove, _inode){};
			virtual PageAbs getSize() override
			{
				return sizeof(Remove);
			}
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

		Empty empty;
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
