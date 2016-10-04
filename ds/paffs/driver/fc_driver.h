/*
 * fc_driver.h
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */

#ifndef DS_PAFFS_DRIVER_FC_DRIVER_H_
#define DS_PAFFS_DRIVER_FC_DRIVER_H_

#include <stddef.h>

typedef void c_flashCell;

struct context{
	int package;
};


p_dev* paffs_FC_install_drv(const char *name, c_flashCell* fc);

//See paffs.h struct p_drv
PAFFS_RESULT p_FC_WritePage(struct p_dev *dev, unsigned long long page_no,
								void* data, unsigned int data_len);
PAFFS_RESULT p_FC_ReadPage(struct p_dev *dev, unsigned long long page_no,
								void* data, unsigned int data_len);
PAFFS_RESULT p_FC_EraseBlock(struct p_dev *dev, unsigned long block_no);
PAFFS_RESULT p_FC_MarkBad(struct p_dev *dev, unsigned long block_no);
PAFFS_RESULT p_FC_CheckBad(struct p_dev *dev, unsigned long block_no);
PAFFS_RESULT p_FC_Initialize(struct p_dev *dev);
PAFFS_RESULT p_FC_Deinitialize(struct p_dev *dev);


#endif /* DS_PAFFS_DRIVER_FC_DRIVER_H_ */
