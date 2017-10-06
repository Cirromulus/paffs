/*
 * journalTopics.hpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

#pragma once

#include "journalEntry.hpp"
#include <map>

namespace paffs
{
	class JournalTopic{
	private:
		JournalEntryBuffer<100> buffer;
	public:
		virtual
		~JournalTopic(){};
		virtual
		JournalEntry::Topic getTopic() = 0;
		void
		enqueueEntry(const JournalEntry& entry);
		void
		finalize();
	protected:
		virtual void
		processEntry(JournalEntry& entry) = 0;
	};
}
