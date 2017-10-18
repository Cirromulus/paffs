/*
 * journalTopics.hpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

#pragma once

#include "commonTypes.hpp"
#include "journalEntry.hpp"

namespace paffs
{

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
	size_t lastCheckpointEnd;
	size_t curr;
	journalEntry::Transaction::Status taStatus;
public:
	JournalEntryBuffer()
	{
		clear();
	}
	Result insert(const JournalEntry &entry)
	{
		if(entry.topic == JournalEntry::Topic::transaction)
		{
			journalEntry::Transaction::Status status =
					static_cast<const journalEntry::Transaction*>(&entry)->status;
			switch(status)
			{
			case journalEntry::Transaction::Status::checkpoint:
				lastCheckpointEnd = list.getWatermark();
				taStatus = status;
				break;
			case journalEntry::Transaction::Status::success:
				if(taStatus != journalEntry::Transaction::Status::checkpoint)
				{
					PAFFS_DBG(PAFFS_TRACE_BUG, "Tried finalizing a non-succeeded Transaction !");
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
	void clear()
	{
		list.clear();
		lastCheckpointEnd = 0;
		curr              = 0;
		taStatus          = journalEntry::Transaction::Status::success;
	}
};

class JournalTopic{
protected:
	JournalEntryBuffer<journalTopicLogSize> *buffer = nullptr;
public:
	virtual
	~JournalTopic(){};
	virtual JournalEntry::Topic
	getTopic() = 0;
	void
	setJournalBuffer(JournalEntryBuffer<journalTopicLogSize> *buf);
	void
	enqueueEntry(const JournalEntry& entry);
	void
	finalize();
protected:
	virtual void
	processEntry(JournalEntry& entry) = 0;
	virtual void
	processUnsucceededEntry(JournalEntry&){};
};
}
