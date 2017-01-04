/*
 * driver_conf.c
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */
#include "driver.h"
#include "fc_driver.h"
#include "../paffs.h"

 unsigned int paffs_trace_mask =
		PAFFS_TRACE_AREA |
		PAFFS_TRACE_ERROR |
		PAFFS_TRACE_BUG |
		//PAFFS_TRACE_TREE |
		//PAFFS_TRACE_CACHE |
		//PAFFS_TRACE_SCAN |
		//PAFFS_TRACE_WRITE |
		//PAFFS_TRACE_SUPERBLOCK |
		//PAFFS_TRACE_ALLOCATE |
		PAFFS_TRACE_VERIFY_AS |
		PAFFS_TRACE_GC |
		PAFFS_TRACE_GC_DETAIL |
		0;

PAFFS_RESULT paffs_start_up(){
	return paffs_custom_start_up(NULL);
}

PAFFS_RESULT paffs_custom_start_up(void* fc){
	static bool start_up_called = false;
	if(start_up_called)
		return PAFFS_FAIL;
	start_up_called = true;


	p_dev *dev;
	if(!(dev = paffs_FC_install_drv("1", fc))){
		return PAFFS_FAIL;
	}

	return paffs_initialize(dev);
}


