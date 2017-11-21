/*
 * Copyright (c) 2016-2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2016-2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#pragma once

#include "commonTypes.hpp"

namespace paffs
{
class GarbageCollection
{
    Device* dev;

public:
    inline GarbageCollection(Device* mdev) : dev(mdev){};

    /* Special case: Target=unset.
     * This frees any Type (with a favour to areas with committed AS'es)
     */
    Result
    collectGarbage(AreaType target);

    /**
     *	Moves all valid Pages to new Area.
     */
    Result
    moveValidDataToNewArea(AreaPos srcArea, AreaPos dstArea, SummaryEntry* summary);

private:
    PageOffs
    countDirtyPages(SummaryEntry* summary);
    AreaPos
    findNextBestArea(AreaType target, SummaryEntry* summaryOut, bool* srcAreaContainsData);
};
}
