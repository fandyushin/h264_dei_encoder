#ifndef _VIDEO_H
#define _VIDEO_H

#include <xdc/std.h>

#include <ti/sdo/dmai/Fifo.h>
#include <ti/sdo/dmai/Pause.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/ce/Engine.h>

/* Environment passed when creating the thread */
typedef struct VideoEnv {
    Rendezvous_Handle hRendezvousInit;
    Rendezvous_Handle hRendezvousCleanup;
    Rendezvous_Handle hRendezvousWriter;

    Pause_Handle hPauseProcess;
    Fifo_Handle hWriterInFifo;
    Fifo_Handle hWriterOutFifo;
    Fifo_Handle hVideoInFifo;
    Fifo_Handle hVideoOutFifo;

    Char *videoEncoder;
    Engine_Handle hEngine;
    
    Int32 outBufSize;
    Int videoBitRate;
    Int videoFrameRate;
    Int32 imageWidth;
    Int32 imageHeight;
} VideoEnv;

/* Thread function prototype */
extern Void *videoThrFxn(Void *arg);

#endif /* _VIDEO_H */