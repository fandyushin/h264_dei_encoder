#ifndef _WRITER_H
#define _WRITER_H

#include <xdc/std.h>

#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Pause.h>
#include <ti/sdo/dmai/Rendezvous.h>

/* Environment passed when creating the thread */
typedef struct WriterEnv {
    Rendezvous_Handle hRendezvousInit;
    Rendezvous_Handle hRendezvousCleanup;
    Pause_Handle hPauseProcess;
    Fifo_Handle hWriterInFifo;
    Fifo_Handle hWriterOutFifo;
    Char *videoFile;
    Int32 outBufSize;
} WriterEnv;

/* Thread function prototype */
extern Void *writerThrFxn(Void *arg);

#endif /* _WRITER_H */