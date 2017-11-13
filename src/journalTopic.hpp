/*
 * journalTopics.hpp
 *
 *  Created on: 22.09.2017
 *      Author: Pascal Pieper
 */

#pragma once

#include "commonTypes.hpp"
#include "journalEntry.hpp"

namespace paffs
{

class JournalTopic{
public:
	virtual
	~JournalTopic(){};
	virtual JournalEntry::Topic
	getTopic() = 0;
	virtual void
	processEntry(JournalEntry& entry) = 0;
	virtual void
	processUncheckpointedEntry(JournalEntry&){};
};
}
