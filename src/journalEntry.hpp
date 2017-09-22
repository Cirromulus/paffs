/*
 * journalEntry.hpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

#pragma once
#include "commonTypes.hpp"
#include <ostream>

namespace paffs
{
using namespace std;

typedef uint32_t TransactionNumber;
typedef uint32_t TreeNodeId;

struct JournalEntry{
	enum class Topic{
		superblock,
		treeCache,
		summaryCache,
		inode,
	};
	Topic mTopic;
protected:
	JournalEntry(Topic type) : mTopic(type){};
public:
	virtual ~JournalEntry(){};
	friend ostream &operator<<( ostream &output, const JournalEntry &in ) {
		output << "Type: " << static_cast<unsigned int>(in.mTopic);
		return output;
	}
};

namespace journalEntry{

	struct Superblock : public JournalEntry{
		enum class Subtype{
			rootnode,
			areaMap,
			commit		//FIXME: Probably not needed
		};
		Subtype mSubtype;
	protected:
		Superblock(Subtype subtype) : JournalEntry(Topic::superblock),
				mSubtype(subtype){};
	public:
		virtual ~Superblock(){};
		friend ostream &operator<<( ostream &output, const Superblock &in ) {
			output << static_cast<const JournalEntry &>(in) <<
					" Subtype: " << static_cast<unsigned int>(in.mSubtype);
			return output;
		}
	};

	namespace superblock{

		struct Rootnode : public Superblock{
			Rootnode(Addr rootnode) : Superblock(Subtype::rootnode),
					mRootnode(rootnode){};
			Addr mRootnode;
			friend ostream &operator<<( ostream &output, const Rootnode &in ) {
				output << static_cast<const Superblock &>(in) <<
						" Addr: " << in.mRootnode;
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
			AreaPos mOffs;
			Element mElement;
		protected:
			AreaMap(AreaPos offs, Element element) : Superblock(Superblock::Subtype::areaMap),
					mOffs(offs), mElement(element){};
		public:
			virtual ~AreaMap(){};
			friend ostream &operator<<( ostream &output, const AreaMap &in ) {
				output << static_cast<const Superblock &>(in) <<
						" Pos: " << in.mOffs << " Element: " <<
						static_cast<unsigned int>(in.mElement);
				return output;
			}
		};

		namespace areaMap {

			struct Type : public AreaMap{
				Type(AreaPos offs, AreaType type) : AreaMap(offs, Element::type),
						mType(type){};
				AreaType mType;
				friend ostream &operator<<( ostream &output, const Type &in ) {
					output << static_cast<const AreaMap &>(in) <<
							" AreaType: " << in.mType;
					return output;
				}
			};
			struct Status : public AreaMap{
				Status(AreaPos offs, AreaStatus status) : AreaMap(offs, Element::status),
						mStatus(status){};
				AreaStatus mStatus;
				friend ostream &operator<<( ostream &output, const Status &in ) {
					output << static_cast<const AreaMap &>(in) <<
							" AreaStatus: " << in.mStatus;
					return output;
				}
			};
			struct Erasecount : public AreaMap{
				Erasecount(AreaPos offs, uint32_t erasecount) : AreaMap(offs, Element::erasecount),
						mErasecount(erasecount){};
				uint32_t mErasecount;
				friend ostream &operator<<( ostream &output, const Erasecount &in ) {
					output << static_cast<const AreaMap &>(in) <<
							" Erasecount: " << in.mErasecount;
					return output;
				}
			};
			struct Position : public AreaMap{
				Position(AreaPos offs, AreaPos position) : AreaMap(offs, Element::position),
						mPosition(position){};
				AreaPos mPosition;
			friend ostream &operator<<( ostream &output, const Position &in ) {
				output << static_cast<const AreaMap &>(in) <<
						" Position: " << in.mPosition;
				return output;
			}
			};
		};

		struct Commit : public Superblock{
			Commit() : Superblock(Subtype::commit){};
		};
	};

	struct TreeCache : public JournalEntry{
		enum class Subtype{
			transaction,
			treeModify
		};
		Subtype mSubtype;
	protected:
		TreeCache(Subtype subtype) : JournalEntry(Topic::treeCache), mSubtype(subtype){};
	public:
		~TreeCache(){};
	};

	namespace treeCache{
		struct Transaction : public TreeCache{
			enum class Operation{
				start,
				end,
				success,
			};
			Operation mOperation;
			TransactionNumber mNumber;
			Transaction(Operation operation, TransactionNumber number) :
						TreeCache(Subtype::transaction), mOperation(operation), mNumber(number){};
		};

		struct TreeModify : public TreeCache{
			enum class Operation{
				add,
				keyInsert,
				inodeInsert,
				keyDelete,
				remove,
				commit
			};
			Addr mSelf;			//May be zero if not in flash
			TreeNodeId mId;		//Physical representation in RAM
			Operation mOp;
		protected:
			TreeModify(Addr self, TreeNodeId id, Operation op) : TreeCache(Subtype::treeModify),
						mSelf(self), mId(id), mOp(op){};
		public:
			virtual ~TreeModify(){};
		};

		namespace treeModify{
			struct Add : public TreeModify{
				bool mIsLeaf;
				Add(Addr self, TreeNodeId id, bool isLeaf) : TreeModify(self, id, Operation::add),
						mIsLeaf(isLeaf){};
			};

			struct KeyInsert : public TreeModify{
				InodeNo mKey;
				KeyInsert(Addr self, TreeNodeId id, InodeNo key) : TreeModify(self, id, Operation::keyInsert),
						mKey(key){};
			};

			struct InodeInsert : public TreeModify{
				Inode mInode;
				InodeInsert(Addr self, TreeNodeId id, Inode inode) : TreeModify(self, id, Operation::inodeInsert),
						mInode(inode){};
			};

			struct KeyDelete : public TreeModify{
				InodeNo mKey;
				KeyDelete(Addr self, TreeNodeId id, InodeNo key) : TreeModify(self, id, Operation::keyDelete),
						mKey(key){};
			};

			struct Remove : public TreeModify{
				Remove(Addr self, TreeNodeId id) : TreeModify(self, id, Operation::remove){};
			};

			struct Commit : public TreeModify{
				Commit(Addr self, TreeNodeId id) : TreeModify(self, id, Operation::commit){};
			};
		};
	};

	struct SummaryCache : public JournalEntry{
		AreaPos mArea;
		SummaryEntry mStatus;
		SummaryCache(AreaPos area, SummaryEntry status) : JournalEntry(Topic::summaryCache),
					mArea(area), mStatus(status){};
	};

	struct PAC : public JournalEntry{
		enum class Subtype{
			write,
			remove
		};
		Subtype mSubtype;
		InodeNo inode;
		//TODO
	};

};
}
