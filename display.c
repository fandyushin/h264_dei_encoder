#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Display.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include "display.h"
#include "main.h"

#define NUM_BUFS 3

Void *displayThrFxn(Void *arg)
{
	DisplayEnv *envp = (DisplayEnv *) arg;
	Void *status = THREAD_SUCCESS;

	Display_Handle hDisplay = NULL;
	Display_Attrs dAttrs;
	ColorSpace_Type colorSpace = ColorSpace_YUV420PSEMI;

	Buffer_Handle gBuf, pBuf;

	BufTab_Handle hDispTab;
	Int bufSize = 0;
	BufferGfx_Attrs  gfxAttrs = BufferGfx_Attrs_DEFAULT;

	/* ▼▼▼▼▼ Initialization ▼▼▼▼▼ */
	BufferGfx_calcDimensions(VideoStd_D1_PAL, colorSpace, &gfxAttrs.dim);
	gfxAttrs.dim.width = 704;
	gfxAttrs.dim.height = 576;
	gfxAttrs.dim.lineLength = Dmai_roundUp(BufferGfx_calcLineLength(gfxAttrs.dim.width, colorSpace), 32);
	gfxAttrs.colorSpace = colorSpace;
	bufSize = gfxAttrs.dim.lineLength * gfxAttrs.dim.height * 3 / 2;
	hDispTab = BufTab_create(NUM_BUFS, bufSize, BufferGfx_getBufferAttrs(&gfxAttrs));
	if (hDispTab == NULL) {
		ERR("Failed to create display buftab\n");
		cleanup(THREAD_FAILURE);
	}

	/* Create the display display driver instance */
	dAttrs = Display_Attrs_DM365_VID_DEFAULT;
    dAttrs.numBufs = NUM_BUFS;
	dAttrs.colorSpace  = colorSpace;
	dAttrs.videoStd = VideoStd_D1_PAL;
	dAttrs.videoOutput = Display_Output_COMPOSITE;
	dAttrs.width = 704;
	dAttrs.height = 576;
	hDisplay = Display_create(hDispTab, &dAttrs);
	if (hDisplay == NULL) {
		ERR("Failed to create display device\n");
		cleanup(THREAD_FAILURE);
	}

	Rendezvous_meet(envp->hRendezvousInit);
	/* ▲▲▲▲▲ Initialization ▲▲▲▲▲ */

	while (1) {
		if (Display_get(hDisplay, &gBuf) < 0) {
			ERR("Failed to get display buffer\n");
			cleanup(THREAD_FAILURE);
		}
		/* Send buffer to dei thread */
		if (Fifo_put(envp->hDisplayOutFifo, gBuf) < 0) {
			ERR("Failed to send buffer to dei thread\n");
			cleanup(THREAD_FAILURE);
		}
		/* Recieve buffer from dei thread */
		if (Fifo_get(envp->hDisplayInFifo, &pBuf) < 0) {
			ERR("Failed to get buffer from dei thread\n");
			cleanup(THREAD_FAILURE);
		}
		if (Display_put(hDisplay, pBuf) < 0) {
			ERR("Failed to put display buffer\n");
			cleanup(THREAD_FAILURE);
		}
	}

cleanup:
	Rendezvous_force(envp->hRendezvousInit);
	Rendezvous_meet(envp->hRendezvousCleanup);

	if (hDisplay) {
		Display_delete(hDisplay);
	}

	if (hDispTab) {
		BufTab_delete(hDispTab);
	}

	return status;
}