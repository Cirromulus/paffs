/*
 * driverconf.hpp
 *
 *  Created on: 17.01.2017
 *      Author: Pascal Pieper
 */

#pragma once

#include "driver.hpp"
#include "simuDriver.hpp"
#include <outpost/rtos/clock.h>

namespace paffs {
	outpost::rtos::SystemClock clock;

	Driver* getDriver(const char* devicename){
		Driver* out = new SimuDriver(clock);
		out->param.name = devicename;
		return out;
	}

	Driver* getDriverSpecial(const char* devicename, void* fc){
		if(fc == NULL){
			std::cerr << "Invalid flashCell pointer given!" << std::endl;
			return NULL;
		}
		Driver* out = new SimuDriver(clock, fc);
		out->param.name = devicename;
		return out;
	}

}
