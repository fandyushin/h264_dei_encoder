#ifndef _MAIN_H
#define _MAIN_H

/* Standard Linux headers */
#include <stdio.h>
#include <signal.h>
#include <pthread.h>

#ifndef SVN
#define SVN "Unknown"
#endif

#define xstr(s)   str(s)
#define str(s)	  #s

/* Error message */
#define ERR(fmt, args...) fprintf(stderr, "Error: " fmt, ## args)

/* Function error codes */
#define SUCCESS             0
#define FAILURE             -1

/* Thread error codes */
#define THREAD_SUCCESS      (Void *) 0
#define THREAD_FAILURE      (Void *) -1

/* Global data structure */
typedef struct GlobalData {
    Int             quit;                /* Global quit flag */
    //pthread_mutex_t mutex;               /* Mutex to protect the global data */
} GlobalData;

#define GBL_DATA_INIT { 0 }

/* Global data */
GlobalData gbl;

static inline Int gblGetQuit(void)
{
    Int quit;

    //pthread_mutex_lock(&gbl.mutex);
    quit = gbl.quit;
    //pthread_mutex_unlock(&gbl.mutex);

    return quit;
}

static inline Void gblSetQuit(void)
{
    //pthread_mutex_lock(&gbl.mutex);
    gbl.quit = TRUE;
    //pthread_mutex_unlock(&gbl.mutex);
}

/* Cleans up cleanly after a failure */
#define cleanup(x)                                  \
    status = (x);                                   \
    goto cleanup

#endif /* _MAIN_H */