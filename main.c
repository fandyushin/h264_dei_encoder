#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xdc/std.h>

#include <ti/sdo/ce/CERuntime.h>
#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/ce/Engine.h>

#include "main.h"
#include "capture.h"
#include "dei.h"
#include "video.h"
#include "writer.h"

#define LOGSINITIALIZED 0x1
#define CAPTURETHREADCREATED 0x20
#define DEITHREADCRETED 0x40
#define WRITERTHREADCREATED 0x80
#define VIDEOTHREADCREATED 0x100

#define VIDEO_THREAD_PRIORITY   sched_get_priority_max(SCHED_FIFO) - 1

/* Global variable declarations for this application */
GlobalData gbl = GBL_DATA_INIT;

Int main(Int argc, Char *argv[])
{
	Uns initMask = 0;
	Int status = EXIT_SUCCESS;

	Rendezvous_Attrs rzvAttrs = Rendezvous_Attrs_DEFAULT;
	Fifo_Attrs fAttrs = Fifo_Attrs_DEFAULT;
	Engine_Handle hEngine = NULL;
	Rendezvous_Handle hRendezvousInit = NULL;
	Rendezvous_Handle hRendezvousCleanup = NULL;
    Rendezvous_Handle hRendezvousWriter = NULL;

	pthread_t captureThread;
	pthread_t deiThread;
	pthread_t videoThread;
    pthread_t writerThread;

	CaptureEnv captureEnv;
	DeiEnv deiEnv;
	VideoEnv videoEnv;
    WriterEnv writerEnv;

	pthread_attr_t attr;
    Int numThreads;

    Void *ret;

    /* ▼▼▼▼▼ Initialization ▼▼▼▼▼ */

    printf("DEI videoloop\n");

    /* Zero out the thread environments */
    Dmai_clear(captureEnv);
    Dmai_clear(deiEnv);
    Dmai_clear(videoEnv);
    Dmai_clear(writerEnv);

    /* Initialize the mutex which protects the global data */
    /*pthread_mutex_init(&gbl.mutex, NULL);*/

    /* Initialize Codec Engine runtime */
    CERuntime_init();

    /* Initialize Davinci Multimedia Application Interface */
    Dmai_init();

    Dmai_setLogLevel(Dmai_LogLevel_All);

    /* Initialize codec engine */
    hEngine = Engine_open("alg_server", NULL, NULL);
    if (hEngine == NULL) {
        ERR("Failed to open codec engine\n");
        cleanup(EXIT_FAILURE);
    }

    initMask |= LOGSINITIALIZED;

    /* Determine the number of threads needing synchronization */
    numThreads = 5;

    /* Create the objects which synchronizes the thread init and cleanup */
    hRendezvousInit = Rendezvous_create(numThreads, &rzvAttrs);
    hRendezvousCleanup = Rendezvous_create(numThreads, &rzvAttrs);
    hRendezvousWriter = Rendezvous_create(2, &rzvAttrs);
    if (hRendezvousInit == NULL || hRendezvousCleanup == NULL || hRendezvousWriter == NULL) {
        ERR("Failed to create Rendezvous objects\n");
        cleanup(EXIT_FAILURE);
    }

    /* Initialize the thread attributes */
    if (pthread_attr_init(&attr)) {
        ERR("Failed to initialize thread attrs\n");
        cleanup(EXIT_FAILURE);
    }

    /* Force the thread to use custom scheduling attributes */
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) {
        ERR("Failed to set schedule inheritance attribute\n");
        cleanup(EXIT_FAILURE);
    }

    /* Set the thread to be fifo real time scheduled */
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) {
        ERR("Failed to set FIFO scheduling policy\n");
        cleanup(EXIT_FAILURE);
    }

	/* Create threads */
	/* Create the capture fifos */
	captureEnv.hCaptureInFifo = Fifo_create(&fAttrs);
	captureEnv.hCaptureOutFifo = Fifo_create(&fAttrs);
	if (captureEnv.hCaptureInFifo == NULL || captureEnv.hCaptureOutFifo == NULL) {
		ERR("Failed to open capture fifos\n");
		cleanup(EXIT_FAILURE);
	}

	/* Create the display fifos */
	videoEnv.hVideoInFifo = Fifo_create(&fAttrs);
	videoEnv.hVideoOutFifo = Fifo_create(&fAttrs);
	if (videoEnv.hVideoInFifo == NULL || videoEnv.hVideoOutFifo == NULL) {
		ERR("Failed to open capture fifos\n");
		cleanup(EXIT_FAILURE);
	}

    /* Create the writer fifos */
    writerEnv.hInFifo = Fifo_create(&fAttrs);
    writerEnv.hOutFifo = Fifo_create(&fAttrs);
    if (writerEnv.hInFifo == NULL || writerEnv.hOutFifo == NULL) {
        ERR("Failed to open display fifos\n");
        cleanup(EXIT_FAILURE);
    }

	/* Capture thread */
	captureEnv.hRendezvousInit = hRendezvousInit;
	captureEnv.hRendezvousCleanup = hRendezvousCleanup;

	if (pthread_create(&captureThread, NULL, captureThrFxn, &captureEnv)) {
		ERR("Failed to create capture thread\n");
		cleanup(EXIT_FAILURE);
	}
	initMask |= CAPTURETHREADCREATED;

	/* Dei thread */
	deiEnv.hRendezvousInit = hRendezvousInit;
	deiEnv.hRendezvousCleanup = hRendezvousCleanup;
	deiEnv.hFromCaptureFifo = captureEnv.hCaptureOutFifo;
	deiEnv.hToCaptureFifo = captureEnv.hCaptureInFifo;
	deiEnv.hFromDisplayFifo = videoEnv.hVideoOutFifo;
	deiEnv.hToDisplayFifo = videoEnv.hVideoInFifo;
	deiEnv.hEngine = hEngine;
	if (pthread_create(&deiThread, NULL, deiThrFxn, &deiEnv)) {
		ERR("Failed to create dei thread\n");
		cleanup(EXIT_FAILURE);
	}
	initMask |= DEITHREADCRETED;

	/* Video thread */
    /* Set the video thread priority */
    schedParam.sched_priority = VIDEO_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(&attr, &schedParam)) {
        ERR("Failed to set scheduler parameters\n");
        cleanup(EXIT_FAILURE);
    }

    /* Create the video thread */
    videoEnv.hRendezvousInit = hRendezvousInit;
    videoEnv.hRendezvousCleanup = hRendezvousCleanup;
    videoEnv.hRendezvousWriter = hRendezvousWriter;
    videoEnv.hPauseProcess = hPauseProcess;
    videoEnv.hWriterOutFifo = writerEnv.hOutFifo;
    videoEnv.hWriterInFifo = writerEnv.hInFifo;
    videoEnv.videoEncoder = "h264enc";
    videoEnv.params = args.videoEncoder->params;
    videoEnv.dynParams = args.videoEncoder->dynParams;
    videoEnv.videoBitRate = 2000000;
    videoEnv.imageWidth = 704;
    videoEnv.imageHeight = 576;
    videoEnv.videoFrameRate = 25000;
    videoEnv.hEngine = hEngine;

    if (pthread_create(&videoThread, &attr, videoThrFxn, &videoEnv)) {
        ERR("Failed to create video thread\n");
        cleanup(EXIT_FAILURE);
    }
    initMask |= VIDEOTHREADCREATED;

    /*
     * Wait for the codec to be created in the video thread before
     * launching the writer thread (otherwise we don't know which size
     * of buffers to use).
     */
    Rendezvous_meet(hRendezvousWriter);    

    /* Writer thread */
    writerEnv.hRendezvousInit = hRendezvousInit;
    writerEnv.hRendezvousCleanup = hRendezvousCleanup;
    writerEnv.hPauseProcess = hPauseProcess;
    writerEnv.videoFile = "/mnt/nfs/video.264";
    writerEnv.outBufSize = videoEnv.outBufSize;

    if (pthread_create(&writerThread, NULL, writerThrFxn, &writerEnv)) {
        ERR("Failed to create writer thread\n");
        cleanup(EXIT_FAILURE);
    }
    initMask |= WRITERTHREADCREATED;

	Rendezvous_meet(hRendezvousInit);
	fprintf(stderr, "Initialization complete\n");
    /* ▲▲▲▲▲ Initialization ▲▲▲▲▲ */
    while (1) {
    	fprintf(stderr, "loop...loop\n");
    	sleep(3);
    }

cleanup:
	Rendezvous_meet(hRendezvousCleanup);
	if (hRendezvousInit) {
		Rendezvous_force(hRendezvousInit);
	}

	if (initMask & DEITHREADCRETED) {
        if (pthread_join(deiThread, &ret) == 0) {
            if (ret == THREAD_FAILURE) {
                status = EXIT_FAILURE;
            }
        }
    }
    if (initMask & CAPTURETHREADCREATED) {
        if (pthread_join(captureThread, &ret) == 0) {
            if (ret == THREAD_FAILURE) {
                status = EXIT_FAILURE;
            }
        }
    }

    if (initMask & DISPLAYTHREADCREATED) {
        if (pthread_join(displayThread, &ret) == 0) {
            if (ret == THREAD_FAILURE) {
                status = EXIT_FAILURE;
            }
        }
    }

    if (captureEnv.hCaptureInFifo) {
        Fifo_delete(captureEnv.hCaptureInFifo);
    }

	if (captureEnv.hCaptureOutFifo) {
        Fifo_delete(captureEnv.hCaptureOutFifo);
    }

    if (displayEnv.hDisplayInFifo) {
        Fifo_delete(displayEnv.hDisplayInFifo);
    }

	if (displayEnv.hDisplayOutFifo) {
        Fifo_delete(displayEnv.hDisplayOutFifo);
    }

    if (hEngine) {
    	Engine_close(hEngine);
    }

    if (hRendezvousCleanup) {
        Rendezvous_delete(hRendezvousCleanup);
    }

    if (hRendezvousInit) {
        Rendezvous_delete(hRendezvousInit);
    }

	exit(status);
}