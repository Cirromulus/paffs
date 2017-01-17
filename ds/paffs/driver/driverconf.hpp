/*
 * driverconf.hpp
 *
 *  Created on: 17.01.2017
 *      Author: rooot
 */

#pragma once

#include "driver.hpp"
#include "simuDriver.hpp"

namespace paffs {

//TODO: getDriver(std::string)

	Driver* getDriver(unsigned int number){
		return new SimuDriver();
	}

	Driver* getDriverSpecial(unsigned int number, void* fc){
		return new SimuDriver((FlashCell*)fc);
	}

}
