#ifndef _DEI_H
#define _DEI_H

#include <xdc/std.h>

#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/ce/Engine.h>


typedef struct DeiEnv {
	Rendezvous_Handle hRendezvousInit;
	Rendezvous_Handle hRendezvousCleanup;

	// With Capture thread
	Fifo_Handle hFromCaptureFifo;;
	Fifo_Handle hToCaptureFifo;

	// With Display thread
	Fifo_Handle hFromDisplayFifo;
	Fifo_Handle hToDisplayFifo;

	Engine_Handle hEngine;
} DeiEnv;

extern Void *deiThrFxn(Void *arg);

#endif /* _DEI_H */