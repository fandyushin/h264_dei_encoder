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
#include "display.h"

#define LOGSINITIALIZED 0x1
#define CAPTURETHREADCREATED 0x20
#define DEITHREADCRETED 0x40
#define DISPLAYTHREADCREATED 0x80

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

	pthread_t captureThread;
	pthread_t deiThread;
	pthread_t displayThread;

	CaptureEnv captureEnv;
	DeiEnv deiEnv;
	DisplayEnv displayEnv;

	pthread_attr_t attr;
    Int numThreads;

    Void *ret;

    /* ▼▼▼▼▼ Initialization ▼▼▼▼▼ */

    printf("DEI videoloop\n");

    /* Zero out the thread environments */
    Dmai_clear(captureEnv);
    Dmai_clear(deiEnv);
    Dmai_clear(displayEnv);

    /* Initialize the mutex which protects the global data */
    //pthread_mutex_init(&gbl.mutex, NULL);

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
    numThreads = 4;

    /* Create the objects which synchronizes the thread init and cleanup */
    hRendezvousInit = Rendezvous_create(numThreads, &rzvAttrs);
    hRendezvousCleanup = Rendezvous_create(numThreads, &rzvAttrs);
    if (hRendezvousInit == NULL || hRendezvousCleanup == NULL) {
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
	displayEnv.hDisplayInFifo = Fifo_create(&fAttrs);
	displayEnv.hDisplayOutFifo = Fifo_create(&fAttrs);
	if (displayEnv.hDisplayInFifo == NULL || displayEnv.hDisplayOutFifo == NULL) {
		ERR("Failed to open capture fifos\n");
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
	deiEnv.hFromDisplayFifo = displayEnv.hDisplayOutFifo;
	deiEnv.hToDisplayFifo = displayEnv.hDisplayInFifo;
	deiEnv.hEngine = hEngine;
	if (pthread_create(&deiThread, NULL, deiThrFxn, &deiEnv)) {
		ERR("Failed to create dei thread\n");
		cleanup(EXIT_FAILURE);
	}
	initMask |= DEITHREADCRETED;

	/* Display thread */
	displayEnv.hRendezvousInit = hRendezvousInit;
	displayEnv.hRendezvousCleanup = hRendezvousCleanup;
	if (pthread_create(&displayThread, NULL, displayThrFxn, &displayEnv)) {
		ERR("Failed to create display thread\n");
		cleanup(EXIT_FAILURE);
	}
	initMask |= DEITHREADCRETED;

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