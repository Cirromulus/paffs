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


	Driver* getDriver(const char* devicename){
		Driver* out = new SimuDriver();
		out->param.name = devicename;
		return out;
	}

	Driver* getDriverSpecial(const char* devicename, void* fc){
		if(fc == NULL){
			std::cerr << "Invalid flashCell pointer given!" << std::endl;
			return NULL;
		}
		Driver* out = new SimuDriver(fc);
		out->param.name = devicename;
		return out;
	}

}
