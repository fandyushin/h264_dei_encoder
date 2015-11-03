#ifndef _CAPTURE_H
#define _CAPTURE_H

#include <xdc/std.h>

#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Rendezvous.h>

typedef struct CaptureEnv {
	Rendezvous_Handle hRendezvousInit;
	Rendezvous_Handle hRendezvousCleanup;

	Fifo_Handle hCaptureInFifo;
	Fifo_Handle hCaptureOutFifo;
} CaptureEnv;

extern Void *captureThrFxn(Void *arg);

#endif /* _CAPTURE_H */