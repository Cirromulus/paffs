/*
 * commonTest.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#include "commonTest.hpp"

using namespace paffs;

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


