/*
 * garbage_collection.h
 *
 *  Created on: 28.12.2016
 *      Author: urinator
 */

#pragma once

#include "paffs.hpp"

namespace paffs {

Result collectGarbage(Device* dev, AreaType target);

}
