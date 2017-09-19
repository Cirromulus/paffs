/*
 * journal.hpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#pragma once
#include "commonTypes.hpp"
#include <ostream>

namespace paffs{
using namespace std;

namespace journalEntry{

struct JournalEntry{
	enum class Type{
		superblock,
		treeCache,
		areaMap,
		inode
	};
protected:
	JournalEntry(Type type) : mType(type){};
public:
	Type mType;
	friend ostream &operator<<( ostream &output, const JournalEntry &in ) {
		output << "Type: " << static_cast<unsigned int>(in.mType);
		return output;
	}
};

	struct Superblock : public JournalEntry{
		enum class Subtype{
			rootnode,
			areaMap,
			commit
		};
	protected:
		Superblock(Subtype subtype) : JournalEntry(Type::superblock),
				mSubtype(subtype){};
	public:
		Subtype mSubtype;
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
		protected:
			AreaMap(AreaPos offs, Element element) : Superblock(Superblock::Subtype::areaMap),
					mOffs(offs), mElement(element){};
		public:
			AreaPos mOffs;
			Element mElement;
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
};

class Journal{
public:
	void doSomething();
};
};
