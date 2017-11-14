/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include "commonTest.hpp"

::testing::AssertionResult StringsMatch(const char *a, const char*b){
	if(strlen(a) != strlen(b))
		return ::testing::AssertionFailure() << "Size differs, " << strlen(a)
				<< " != " << strlen(b);

	for(size_t i(0); i < strlen(a); i++){
        if (a[i] != b[i]){
            return ::testing::AssertionFailure() << "array[" << i
                << "] (" << a[i] << ") != expected[" << i
                << "] (" << b[i] << ")";
        }
	}
	return ::testing::AssertionSuccess();
}


