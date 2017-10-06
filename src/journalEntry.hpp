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
		treeCache,
		summaryCache,
		inode,
	};
	Topic topic;
protected:
	JournalEntry(Topic _topic) : topic(_topic){};
public:
	virtual ~JournalEntry(){};
	friend ostream &operator<<( ostream &output, const JournalEntry &in ) {
		output << "Type: " << static_cast<unsigned int>(in.topic);
		return output;
	}
};

namespace journalEntry
{

	struct Transaction : public JournalEntry
	{
		enum class Status
		{
			start,
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
		friend ostream &operator<<( ostream &output, const Superblock &in ) {
			output << static_cast<const JournalEntry &>(in) <<
					" Subtype: " << static_cast<unsigned int>(in.subtype);
			return output;
		}
	};

	namespace superblock{

		struct Rootnode : public Superblock{
			Rootnode(Addr _rootnode) : Superblock(Subtype::rootnode),
					rootnode(_rootnode){};
			Addr rootnode;
			friend ostream &operator<<( ostream &output, const Rootnode &in ) {
				output << static_cast<const Superblock &>(in) <<
						" Addr: " << in.rootnode;
				return output;
			}
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
			friend ostream &operator<<( ostream &output, const AreaMap &in ) {
				output << static_cast<const Superblock &>(in) <<
						" Pos: " << in.offs << " Element: " <<
						static_cast<unsigned int>(in.element);
				return output;
			}
		};

		namespace areaMap
		{
			struct Type : public AreaMap{
				Type(AreaPos _offs, AreaType _type) : AreaMap(_offs, Element::type),
						type(_type){};
				AreaType type;
				friend ostream &operator<<( ostream &output, const Type &in ) {
					output << static_cast<const AreaMap &>(in) <<
							" AreaType: " << in.type;
					return output;
				}
			};
			struct Status : public AreaMap{
				Status(AreaPos _offs, AreaStatus _status) : AreaMap(_offs, Element::status),
						status(_status){};
				AreaStatus status;
				friend ostream &operator<<( ostream &output, const Status &in ) {
					output << static_cast<const AreaMap &>(in) <<
							" AreaStatus: " << in.status;
					return output;
				}
			};
			struct Erasecount : public AreaMap{
				Erasecount(AreaPos _offs, uint32_t _erasecount) : AreaMap(_offs, Element::erasecount),
						erasecount(_erasecount){};
				uint32_t erasecount;
				friend ostream &operator<<( ostream &output, const Erasecount &in ) {
					output << static_cast<const AreaMap &>(in) <<
							" Erasecount: " << in.erasecount;
					return output;
				}
			};
			struct Position : public AreaMap{
				Position(AreaPos _offs, AreaPos _position) : AreaMap(_offs, Element::position),
						position(_position){};
				AreaPos position;
				friend ostream &operator<<( ostream &output, const Position &in ) {
					output << static_cast<const AreaMap &>(in) <<
							" Position: " << in.position;
					return output;
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
			areaMap::Max areaMap;
		};
	};

	struct BTree : public JournalEntry{
		enum class Operation{
			add,
			keyInsert,
			inodeInsert,
			remove,
			//TODO:
			//Move all from pos X to the right
			//delete all from pos X to Y (without removing from cache)
		};
		Operation op;
		Addr self;			//May be zero if not in flash
		TreeNodeId id;		//Physical representation in RAM
	protected:
		BTree(Addr _self, TreeNodeId _id, Operation _operation) : JournalEntry(Topic::treeCache),
				op(_operation), self(_self), id(_id){};
	public:
		~BTree(){};
	};

	namespace btree{
		struct Add : public BTree{
			bool isLeaf;
			TreeNodeId parent;
			Add(Addr _self, TreeNodeId _id, TreeNodeId _parent, bool _isLeaf) : BTree(_self, _id, Operation::add),
					isLeaf(_isLeaf), parent(_parent){};
		};

		struct KeyInsert : public BTree{
			InodeNo key;
			Addr value;

			KeyInsert(Addr _self, TreeNodeId _id, InodeNo _key, Addr _value) : BTree(_self, _id, Operation::keyInsert),
					key(_key), value(_value){};
		};

		struct InodeInsert : public BTree{
			InodeNo key;
			//Inode will be taken from PAC
			InodeInsert(Addr _self, TreeNodeId _id, InodeNo _key) : BTree(_self, _id, Operation::inodeInsert),
					key(_key){};
		};

		struct Remove : public BTree{
			Remove(Addr _self, TreeNodeId _id) : BTree(_self, _id, Operation::remove){};
		};
		union Max
		{
			Add add;
			KeyInsert keyInsert;
			InodeInsert inodeInsert;
			Remove remove;
		};
	};

	struct SummaryCache : public JournalEntry{
		enum class Subtype{
			setStatus,
			commit
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
			case journalEntry::Transaction::Status::start:
				if(taStatus != journalEntry::Transaction::Status::success)
				{
					cout << "Tried starting a new Transaction without stopping old one!" << endl;
					break;
				}
				taStatus = status;
				break;
			case journalEntry::Transaction::Status::end:
				if(taStatus != journalEntry::Transaction::Status::start)
				{
					cout << "Tried stopping a nonexisting Transaction !" << endl;
					break;
				}
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
	JournalEntry* pop()
	{
		if(curr >= lastCheckpointEnd)
			return nullptr;
		return reinterpret_cast<JournalEntry*>(list.get(curr++));
	}
};
}
