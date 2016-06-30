/*
 * fc_driver.c
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */
#include "../../simu/flashCell.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "fc_driver.h"

NANDADRESS translatePageToAddress(unsigned int sector, flashCell* fc){
	NANDADRESS r;
	r.page = sector % fc->blockSize;
	r.block = (sector / fc->blockSize) % fc->planeSize;
	r.plane = (sector / fc->blockSize) / fc->planeSize;
	return r;
}

NANDADRESS translateBlockToAddress(unsigned int block, flashCell* fc){
	NANDADRESS r;
	r.plane = block / fc->planeSize;
	r.block = block % fc->planeSize;
	r.page = 0;
	return r;
}

p_dev* paffs_FC_install_drv(const char *name, c_flashCell* fc){

	if(fc == NULL){
		fc = (c_flashCell*) new flashCell;
	}
	p_dev* dev = (p_dev*)malloc(sizeof(p_dev));
	struct context *con = (struct context*)malloc(sizeof (struct context));
	con->package = 0;	//TODO: hardcoded for now
	dev->driver_context = con;

	c_fc[con->package] = fc;

	dev->drv.drv_write_page_fn = p_FC_WritePage;
	dev->drv.drv_read_page_fn = p_FC_ReadPage;
	dev->drv.drv_erase_fn= p_FC_EraseBlock;
	dev->drv.drv_mark_bad_fn = p_FC_MarkBad;
	dev->drv.drv_check_bad_fn = p_FC_CheckBad;
	dev->drv.drv_initialise_fn = p_FC_Initialize;
	dev->drv.drv_deinitialise_fn = p_FC_Deinitialize;

	dev->param.name = name;
	dev->param.total_bytes_per_page = ((flashCell*)fc)->pageSize;
	dev->param.pages_per_block = ((flashCell*)fc)->blockSize;
	dev->param.blocks = ((flashCell*)fc)->planeSize * ((flashCell*)fc)->cellSize;

	return dev;
}

//See paffs.h struct p_drv
PAFFS_RESULT p_FC_WritePage(struct p_dev *dev, unsigned int page_no,
								void* data, unsigned int data_len){
	flashCell* fc = (flashCell*) c_fc[((struct context*)dev->driver_context)->package];
	if(!fc)
		return PAFFS_FAIL;

	if(dev->param.total_bytes_per_page != data_len){
		fprintf(stderr, "PAFFS: misaligned write!\n");
		return PAFFS_NIMPL;
	}

	NANDADRESS d = translatePageToAddress(page_no, fc);

	if(fc->writePage(d.plane, d.block, d.page, (unsigned char*)data) < 0){
		return PAFFS_FAIL;
	}
	return PAFFS_OK;
}
PAFFS_RESULT p_FC_ReadPage(struct p_dev *dev, unsigned int page_no,
								void* data, unsigned int data_len){
	flashCell* fc = (flashCell*) c_fc[((struct context*)dev->driver_context)->package];
	if(!fc)
		return PAFFS_FAIL;

	if(dev->param.total_bytes_per_page != data_len){
		fprintf(stderr, "PAFFS: misaligned read!\n");
		return PAFFS_NIMPL;
	}

	NANDADRESS d = translatePageToAddress(page_no, fc);

	if(fc->readPage(d.plane, d.block, d.page, (unsigned char*)data) < 0){
		return PAFFS_FAIL;
	}

	return PAFFS_OK;
}
PAFFS_RESULT p_FC_EraseBlock(struct p_dev *dev, unsigned int block_no){
	flashCell* fc = (flashCell*) c_fc[((struct context*)dev->driver_context)->package];
	if(!fc)
		return PAFFS_BUG;

	NANDADRESS d = translateBlockToAddress(block_no, fc);

	return fc->eraseBlock(d.plane, d.block) == 0 ? PAFFS_OK : PAFFS_FAIL;
}
PAFFS_RESULT p_FC_MarkBad(struct p_dev *dev, unsigned int block_no){
	return PAFFS_NIMPL;
}
PAFFS_RESULT p_FC_CheckBad(struct p_dev *dev, unsigned int block_no){
	return PAFFS_NIMPL;
}
PAFFS_RESULT p_FC_Initialize(struct p_dev *dev){
	if(!dev->driver_context)
		return PAFFS_FAIL;
	flashCell* fc = (flashCell*) c_fc[((struct context*)dev->driver_context)->package];
	if(!fc)
		c_fc[((struct context*)dev->driver_context)->package] = (c_flashCell*) new flashCell;
	return PAFFS_OK;
}
PAFFS_RESULT p_FC_Deinitialize(struct p_dev *dev){
	if(!dev->driver_context)
		return PAFFS_FAIL;
	flashCell* fc = (flashCell*) c_fc[((struct context*)dev->driver_context)->package];
	if(!fc)
		return PAFFS_FAIL;

	delete fc;
	free(dev->driver_context);	//FIXME Should Deinitialize free whole dev-context?
	free(dev);

	return PAFFS_OK;
}

#ifdef __cplusplus
}
#endif
