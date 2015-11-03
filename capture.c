#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Capture.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include "capture.h"
#include "main.h"

#define NUM_BUFS 3

Void *captureThrFxn(Void *arg) {
	CaptureEnv *envp = (CaptureEnv *) arg;
	Void *status = THREAD_SUCCESS;

	Capture_Handle hCapture = NULL;
	Capture_Attrs cAttrs;
	ColorSpace_Type colorSpace = ColorSpace_UYVY;

	BufferGfx_Attrs gfxAttrs = BufferGfx_Attrs_DEFAULT;
	Int bufSize = 0;
	BufTab_Handle hCapTab = NULL;
	Buffer_Handle cBuf, dBuf;

	/* ▼▼▼▼▼ Initialization ▼▼▼▼▼ */
	/* BufTab creating */
    BufferGfx_calcDimensions(VideoStd_D1_PAL, colorSpace, &gfxAttrs.dim);
    gfxAttrs.dim.lineLength = Dmai_roundUp(BufferGfx_calcLineLength(gfxAttrs.dim.width, colorSpace), 32);
    gfxAttrs.colorSpace = colorSpace;
    bufSize = gfxAttrs.dim.lineLength * gfxAttrs.dim.height;
    hCapTab = BufTab_create(NUM_BUFS, bufSize, BufferGfx_getBufferAttrs(&gfxAttrs));
	if (hCapTab == NULL) {
		ERR("Failed to create capture buftab\n");
		cleanup(THREAD_FAILURE);
	}

	/* Create the capture display driver instance */
	cAttrs = Capture_Attrs_DM365_DEFAULT;   
	cAttrs.numBufs = NUM_BUFS;
	cAttrs.colorSpace = colorSpace;
	cAttrs.videoInput = Capture_Input_COMPOSITE;
	cAttrs.videoStd = VideoStd_D1_PAL;
    cAttrs.onTheFly = FALSE;
    hCapture = Capture_create(hCapTab, &cAttrs);
    if (hCapture == NULL) {
        ERR("Failed to create capture device\n");
        cleanup(THREAD_FAILURE);
    }

    Rendezvous_meet(envp->hRendezvousInit);
	/* ▲▲▲▲▲ Initialization ▲▲▲▲▲ */
	
	while (1) {
		/* Capture a frame */
		if (Capture_get(hCapture, &cBuf) < 0) {
			ERR("Failed to get capture buffer\n");
			cleanup(THREAD_FAILURE);
		}

		/* Send buffer to dei thread for de-interlacing */
		if (Fifo_put(envp->hCaptureOutFifo, cBuf) < 0) {
			ERR("Failed to send buffer to dei thread\n");
			cleanup(THREAD_FAILURE);
		}

		/* Recieve buffer from dei thread for capturing */
		if (Fifo_get(envp->hCaptureInFifo, &dBuf) < 0) {
			ERR("Failed to get buffer from dei thread\n");
			cleanup(THREAD_FAILURE);
		}

		/* Release buffer to the display device driver */
		if (Capture_put(hCapture, dBuf) < 0) {
			ERR("Failed to put display buffer\n");
			cleanup(THREAD_FAILURE);
		}
	}

cleanup:
	Rendezvous_force(envp->hRendezvousInit);
	Rendezvous_meet(envp->hRendezvousCleanup);

	if (hCapture) {
		Capture_delete(hCapture);
	}

	if (hCapTab) {
		BufTab_delete(hCapTab);
	}

	return status;
}