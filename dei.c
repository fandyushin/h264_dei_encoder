#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/codecs/dei/idei.h>
#include <ti/sdo/ce/video1/videnc1.h>
#include <ti/sdo/linuxutils/cmem/include/cmem.h>
#include <dm365mm.h>

#include "dei.h"
#include "main.h"

#define DM365MMAP_IOCMEMCPY        0x7
#define DM365MMAP_IOCWAIT          0x8
#define DM365MMAP_IOCCLEAR_PENDING 0x9

#define IN_OUT_BUF_SIZE	 (1024 * 1024) 

typedef struct {
    unsigned long src;
    unsigned long dst;
    unsigned int  srcmode;
    unsigned int  srcfifowidth;
    int           srcbidx;
    int           srccidx;
    unsigned int  dstmode;
    unsigned int  dstfifowidth;
    int           dstbidx;
    int           dstcidx;
    int           acnt;
    int           bcnt;
    int           ccnt;
    int           bcntrld;
    int           syncmode;
} dm365mmap_params_t;


static Uint32 DM365MM_mmap(off_t addr, size_t size)
{
	Uint32 ret = 0;
	Int fm = 0;

	fm = open("/dev/mem", O_RDWR|O_SYNC);
	ret = (Uint32)mmap((void *)0, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, fm, addr);

	return ret;
}

static Void DM365MM_ummap(off_t addr, size_t size) 
{
	munmap((void *)addr, size);
}

Void *deiThrFxn(Void *arg)
{
	DeiEnv *envp = (DeiEnv *) arg;
	Void *status = THREAD_SUCCESS;

	Uint32 sysRegBase = 0;

	VIDENC1_Handle hDei = NULL;
	IDEI_Params deiParams;

	Uint16 frame_width = 0, frame_height = 0;
	Uint16 threshold_low = 0, threshold_high = 0;

	IVIDEO1_BufDescIn inBufDesc;	
	XDM_BufDesc outBufDesc;	
	XDAS_Int8 *outbufs[2];	
	XDAS_Int32 outBufSizeArray[2];
	VIDENC1_InArgs inArgs;
	IDEI_OutArgs outArgs;	
	Uint32 bufferSize;

	CMEM_AllocParams cmemPrm;
	Uint32 prevBufAddr, outBufAddr;

	Buffer_Handle cBuf, dBuf;

	Int ret = 0;

	int fd = -1;
	dm365mmap_params_t dm365mmap_params;
	pthread_mutex_t Dmai_DM365dmaLock = PTHREAD_MUTEX_INITIALIZER;

	/* ▼▼▼▼▼ Initialization ▼▼▼▼▼ */
	DM365MM_init(); printf("\n"); /* dm365mm issue */
	sysRegBase = DM365MM_mmap(0x01C40000, 0x4000);

	CMEM_init();
	cmemPrm.type = CMEM_HEAP;
	cmemPrm.flags = CMEM_NONCACHED;
	cmemPrm.alignment = 32;
	prevBufAddr = (Uint32)CMEM_alloc(IN_OUT_BUF_SIZE, &cmemPrm);
	outBufAddr = (Uint32)CMEM_alloc(IN_OUT_BUF_SIZE, &cmemPrm);

	frame_width = 720;
	frame_height = 576;
	threshold_low = 16;
	threshold_high = 20;

	/* Create DEI instance */	
	deiParams.videncParams.size = sizeof(IDEI_Params);
	deiParams.frameHeight = frame_height;
	deiParams.frameWidth = frame_width; 
	deiParams.inLineOffset = frame_width;
	deiParams.outLineOffset = (frame_width - 8);	
	deiParams.threshold_low = threshold_low;
	deiParams.threshold_high = threshold_high;

	deiParams.inputFormat = XDM_YUV_422ILE;	
	deiParams.outputFormat = XDM_YUV_420SP;
	
	deiParams.q_num = 1;
	deiParams.askIMCOPRes = 0; 
	deiParams.sysBaseAddr = sysRegBase;

	hDei = VIDENC1_create(envp->hEngine, "dei", (VIDENC1_Params *)&deiParams);						  
    if(hDei == NULL)
	{
		ERR("DEI alg creation failed\n");
        cleanup(THREAD_FAILURE);		
	}

	fd = open("/dev/dm365mmap", O_RDWR | O_SYNC);

	

	Rendezvous_meet(envp->hRendezvousInit);
	/* ▲▲▲▲▲ Initialization ▲▲▲▲▲ */
	while (1) {

		if (Fifo_get(envp->hFromCaptureFifo, &cBuf) < 0) {
			ERR("Failed to get buffer from capture thread\n");
			cleanup(THREAD_FAILURE);
		}

		if (Fifo_get(envp->hFromDisplayFifo, &dBuf) < 0) {
			ERR("Failed to get buffer from display thread\n");
			cleanup(THREAD_FAILURE);
		}

		inBufDesc.numBufs = 4;

		bufferSize = (frame_width * frame_height);
		
		inBufDesc.bufDesc[0].bufSize = bufferSize;
		inBufDesc.bufDesc[0].buf = (XDAS_Int8 *)Buffer_getUserPtr(cBuf);
		inBufDesc.bufDesc[0].accessMask = 0;

		inBufDesc.bufDesc[1].bufSize = bufferSize;
		inBufDesc.bufDesc[1].buf = (XDAS_Int8 *)(Buffer_getUserPtr(cBuf) + bufferSize);
		inBufDesc.bufDesc[1].accessMask = 0;

		inBufDesc.bufDesc[2].bufSize = bufferSize;
		inBufDesc.bufDesc[2].buf = (XDAS_Int8 *)prevBufAddr;
		inBufDesc.bufDesc[2].accessMask = 0;

		inBufDesc.bufDesc[3].bufSize = bufferSize;
		inBufDesc.bufDesc[3].buf = (XDAS_Int8 *)(prevBufAddr + bufferSize);
		inBufDesc.bufDesc[3].accessMask = 0;	
		
		/* Output buffers */
		outBufDesc.numBufs = 2;
		outbufs[0] = (XDAS_Int8*)outBufAddr;
		outbufs[1] = (XDAS_Int8*)(outBufAddr + bufferSize);
		outBufSizeArray[0] = bufferSize;
		outBufSizeArray[1] = bufferSize / 2;

		outBufDesc.bufSizes = outBufSizeArray;
		outBufDesc.bufs = outbufs;

		inArgs.size = sizeof(VIDENC1_InArgs);
		outArgs.videncOutArgs.size = sizeof(IDEI_OutArgs);

		ret = VIDENC1_process((VIDENC1_Handle)hDei,
								 &inBufDesc,
								 &outBufDesc,
								 &inArgs,
								 (IVIDENC1_OutArgs *)&outArgs);
		if (ret != VIDENC1_EOK) {
			ERR("DEI process failed\n");
			cleanup(THREAD_FAILURE);
		}

		dm365mmap_params.src = CMEM_getPhys(outbufs[0]);
		dm365mmap_params.srcmode = 0;
		dm365mmap_params.dst = Buffer_getPhysicalPtr(dBuf);
		dm365mmap_params.dstmode = 0;
		dm365mmap_params.srcbidx = 712;
		dm365mmap_params.dstbidx = 704;
		dm365mmap_params.acnt = 704;
		dm365mmap_params.bcnt = 576;
		dm365mmap_params.ccnt = 1;
		dm365mmap_params.bcntrld = dm365mmap_params.bcnt;
		dm365mmap_params.syncmode = 1;

		pthread_mutex_lock(&Dmai_DM365dmaLock);
		if (ioctl(fd, DM365MMAP_IOCMEMCPY, &dm365mmap_params) == -1) {
        	ERR("memcpy: Failed to do memcpy\n");
        	cleanup(THREAD_FAILURE);
    	}
		pthread_mutex_unlock(&Dmai_DM365dmaLock);

    	dm365mmap_params.src = CMEM_getPhys(outbufs[1]);
    	dm365mmap_params.srcmode = 0;
    	dm365mmap_params.dst = Buffer_getPhysicalPtr(dBuf) + (Buffer_getSize(dBuf) * 2 / 3);
    	dm365mmap_params.dstmode = 0;
    	dm365mmap_params.srcbidx = 712;
		dm365mmap_params.dstbidx = 704;
    	dm365mmap_params.acnt = 712;
    	dm365mmap_params.bcnt = 570 / 2;
    	dm365mmap_params.ccnt = 1;
    	dm365mmap_params.bcntrld = dm365mmap_params.bcnt;
    	dm365mmap_params.syncmode = 1;

		pthread_mutex_lock(&Dmai_DM365dmaLock);
		if (ioctl(fd, DM365MMAP_IOCMEMCPY, &dm365mmap_params) == -1) {
        	ERR("memcpy: Failed to do memcpy\n");
        	cleanup(THREAD_FAILURE);
    	}
		pthread_mutex_unlock(&Dmai_DM365dmaLock);
    	Buffer_setNumBytesUsed(dBuf, 704 * 576 * 3 / 2);

    	dm365mmap_params.src = Buffer_getPhysicalPtr(cBuf);
    	dm365mmap_params.srcmode = 0;
    	dm365mmap_params.dst = prevBufAddr;
    	dm365mmap_params.dstmode = 0;
    	dm365mmap_params.srcbidx = 1440;
		dm365mmap_params.dstbidx = 1440;
    	dm365mmap_params.acnt = 1440;
    	dm365mmap_params.bcnt = 576;
    	dm365mmap_params.ccnt = 1;
    	dm365mmap_params.bcntrld = dm365mmap_params.bcnt;
    	dm365mmap_params.syncmode = 1;

    	pthread_mutex_lock(&Dmai_DM365dmaLock);
		if (ioctl(fd, DM365MMAP_IOCMEMCPY, &dm365mmap_params) == -1) {
        	ERR("memcpy: Failed to do memcpy\n");
        	cleanup(THREAD_FAILURE);
    	}
		pthread_mutex_unlock(&Dmai_DM365dmaLock);

		/* Send buffer to display thread */
		if (Fifo_put(envp->hToDisplayFifo, dBuf) < 0) {
			ERR("Failed to send buffer to dei thread\n");
			cleanup(THREAD_FAILURE);
		}

		/* Send buffer to display thread */
		if (Fifo_put(envp->hToCaptureFifo, cBuf) < 0) {
			ERR("Failed to send buffer to dei thread\n");
			cleanup(THREAD_FAILURE);
		}

	}

cleanup:
	Rendezvous_force(envp->hRendezvousInit);
	Rendezvous_meet(envp->hRendezvousCleanup);

	/* Delete DEI ALG instance */
	VIDENC1_delete(hDei);

	DM365MM_ummap(sysRegBase,0x4000);

	DM365MM_exit();

	if (fd > 0) {   
        close(fd);
    }

	return status;
}