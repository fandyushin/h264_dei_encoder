#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <xdc/std.h>

#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Rendezvous.h>

typedef struct DisplayEnv {
	Rendezvous_Handle hRendezvousInit;
	Rendezvous_Handle hRendezvousCleanup;

	Fifo_Handle hDisplayInFifo;
	Fifo_Handle hDisplayOutFifo;
} DisplayEnv;

extern Void *displayThrFxn(Void *arg);

#endif /* _DISPLAY_H */