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
using namespace std;

typedef uint32_t TreeNodeId;

struct JournalEntry{
	enum class Topic{
		transaction,
		superblock,
		tree,
		summaryCache,
		inode,
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
			end,
			success,
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
				position
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
			areaMap::Max areaMap;
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
			Inode inode;
			Insert(Inode _inode): BTree(Operation::insert), inode(_inode){};
		};
		struct Update : public BTree{
			Inode inode;
			Update(Inode _inode): BTree(Operation::update), inode(_inode){};
		};
		struct Remove : public BTree{
			InodeNo no;
			Remove(InodeNo _no): BTree(Operation::insert), no(_no){};
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
		struct Add : public Inode
		{
			Add(InodeNo _inode) : Inode(Subtype::add, _inode){};
		};

		//... write, remove ... TODO
		union Max
		{
			Add add;
		};
	}
	union Max
	{
		Transaction transaction;
		superblock::Max superblock;
		btree::Max btree;
		summaryCache::Max summaryCache;
		inode::Max inode;
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

template<size_t size> class JournalEntryBuffer
{
	template<typename T> class List
	{
		T list[size];
		size_t wm = 0;
	public:
		paffs::Result add(T* &elem)
		{
			if(wm == size)
				return paffs::Result::nospace;
			elem = &list[wm++];
			return paffs::Result::ok;
		}
		T* get(const size_t pos){
			if(pos >= size)
				return nullptr;
			return &list[pos];
		}
		void clear()
		{
			wm = 0;
		};
		size_t getWatermark(){
			return wm;
		}

	};
	List<journalEntry::Max> list;
	size_t lastCheckpointEnd = 0;
	size_t curr				 = 0;
	journalEntry::Transaction::Status taStatus =
			journalEntry::Transaction::Status::success;
public:
	Result insert(const JournalEntry &entry)
	{
		if(entry.topic == JournalEntry::Topic::transaction)
		{
			journalEntry::Transaction::Status status =
					static_cast<const journalEntry::Transaction*>(&entry)->status;
			switch(status)
			{
			case journalEntry::Transaction::Status::end:
				lastCheckpointEnd = list.getWatermark();
				taStatus = status;
				break;
			case journalEntry::Transaction::Status::success:
				if(taStatus != journalEntry::Transaction::Status::end)
				{
					cout << "Tried finalizing a non-succeeded Transaction !" << endl;
					break;
				}
				list.clear();
				lastCheckpointEnd = 0;
				taStatus = status;
				break;
			}
			return Result::ok;
		}

		journalEntry::Max* n;
		if(list.add(n) == Result::nospace)
			return Result::nospace;
		/*
		 * Fixme: this reads potential uninitialized memory.
		 * Anyway, this should not be a problem, b.c. it wont get read again
		 */
		memcpy(static_cast<void*>(n), static_cast<const void*>(&entry), sizeof(journalEntry::Max));

		return Result::ok;
	}
	void rewind()
	{
		curr = 0;
	}
	void rewindToUnsucceeded()
	{
		curr = lastCheckpointEnd;
	}
	JournalEntry* pop()
	{
		if(curr >= lastCheckpointEnd)
			return nullptr;
		return reinterpret_cast<JournalEntry*>(list.get(curr++));
	}
	JournalEntry* popInvalid()
	{
		if(curr >= list.getWatermark())
			return nullptr;
		return reinterpret_cast<JournalEntry*>(list.get(curr++));
	}
};
}
