#include <stdio.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/BufTab.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Rendezvous.h>

#include "writer.h"
#include "main.h"

/* Number of buffers in writer pipe */
#define NUM_WRITER_BUFS         10

Void *writerThrFxn(Void *arg)
{
    WriterEnv *envp = (WriterEnv *) arg;
    Void *status = THREAD_SUCCESS;
    FILE *outFile = NULL;
    Buffer_Attrs bAttrs = Buffer_Attrs_DEFAULT;
    BufTab_Handle hBufTab = NULL;
    Buffer_Handle hOutBuf;
    Int bufIdx;

    /* Initialization */

	/* Open the output video file */
    outFile = fopen(envp->videoFile, "w");
	if (outFile == NULL) {
        ERR("Failed to open %s for writing\n", envp->videoFile);
        cleanup(THREAD_FAILURE);
    }

    /* Create buftab for video thread */
    hBufTab = BufTab_create(NUM_WRITER_BUFS, envp->outBufSize, &bAttrs);
    if (hBufTab == NULL) {
        ERR("Failed to allocate contiguous buffers\n");
        cleanup(THREAD_FAILURE);
    }

	/* Send all buffers to the video thread to be filled with encoded data */
    for (bufIdx = 0; bufIdx < NUM_WRITER_BUFS; bufIdx++) {
        if (Fifo_put(envp->hWriterOutFifo, BufTab_getBuf(hBufTab, bufIdx)) < 0) {
            ERR("Failed to send buffer to display thread\n");
            cleanup(THREAD_FAILURE);
        }
    }

	/* Signal that initialization is done and wait for other threads */
    Rendezvous_meet(envp->hRendezvousInit);

    while(1) {

    	/* Get an encoded buffer from the video thread */
        if (Fifo_get(envp->hWriterInFifo, &hOutBuf) < 0) {
            ERR("Failed to get buffer from video thread\n");
            cleanup(THREAD_FAILURE);
        }

		if (Buffer_getNumBytesUsed(hOutBuf)) {
			if (fwrite(Buffer_getUserPtr(hOutBuf), Buffer_getNumBytesUsed(hOutBuf), 1, outFile) != 1) {
				ERR("Error writing the encoded data to video file\n");
				cleanup(THREAD_FAILURE);
			}
		} 
		else {
			printf("Warning, writer received 0 byte encoded frame\n");
		}

		/* Return buffer to capture thread */
        if (Fifo_put(envp->hWriterOutFifo, hOutBuf) < 0) {
            ERR("Failed to send buffer to display thread\n");
            cleanup(THREAD_FAILURE);
        }

    }

cleanup:

    /* Make sure the other threads aren't waiting for us */
    Rendezvous_force(envp->hRendezvousInit);
    Pause_off(envp->hPauseProcess);

    /* Meet up with other threads before cleaning up */
    Rendezvous_meet(envp->hRendezvousCleanup);

    /* Clean up the thread before exiting */
    if (outFile) {
        fclose(outFile);
    }

    if (hBufTab) {
        BufTab_delete(hBufTab);
    }
    
	return status;    
}