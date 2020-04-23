/*
phase4.c

Michael Davis, Aaron Posey
4/15/2020
CSCV452 Phase 4
*/

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <usyscall.h>
#include <libuser.h>
#include <provided_prototypes.h>
#include "driver.h"

/* ------------------------- Prototypes ----------------------------------- */

int start3 (char *); 

static int	ClockDriver(char *);
static int	DiskDriver(char *);

static void sleep_first(sysargs *);
//sleep_real prototype in phase4.h
static void disk_read_first(sysargs *);
//disk_read_real prototype in phase4.h
static void disk_write_first(sysargs *);
//disk_write_real prototype in phase4.h
static void disk_size_first(sysargs *);
//disk_size_real prototype in phase4.h

int insert_sleep_q(driver_proc_ptr);

/* -------------------------- Globals ------------------------------------- */

static int debugflag4 = 0;

static int running; /*semaphore to synchronize drivers and start3*/

static struct driver_proc Driver_Table[MAXPROC];

static int diskpids[DISK_UNITS];

static int num_tracks[DISK_UNITS]; //added to address DiskDriver references

struct driver_proc dummy_proc = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

int ZAP_FLAG = 0;

driver_proc_ptr SleepQ = NULL;

int sleep_number = 0;

static int sleep_semaphore;

/* -------------------------- Functions ----------------------------------- */

/* start3 */
int
start3(char *arg)
{
    char	name[128];
    char    termbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;
    int     result;
    int     index;
    int     zap_sig = ZAP_SIGNAL;

    /* Check kernel mode here */
    if ((psr_get() & PSR_CURRENT_MODE) == 0)
    {
        console("start3(): Not in kernel mode\n");
        halt(1);
    }

    /* Assignment system call handlers */
    sys_vec[SYS_SLEEP] = sleep_first;
    sys_vec[SYS_DISKREAD] = disk_read_first;
    sys_vec[SYS_DISKWRITE] = disk_write_first;
    sys_vec[SYS_DISKSIZE] = disk_size_first;
    
    /* Initialize the phase 4 process table */
    for (int i = 0; i < MAXPROC; i++)
    {
        Driver_Table[i] = dummy_proc;
        Driver_Table[i].start_mbox = Mbox_Create(0, MAX_MESSAGE, NULL);
        Driver_Table[i].private_mbox = Mbox_Create(1, MAX_MESSAGE, NULL);
        Driver_Table[i].private_sem = semcreate_real(0);
    }

    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    sleep_semaphore = semcreate_real(1);
    running = semcreate_real(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) 
    {
	    console("start3(): Can't create clock driver\n");
	    halt(1);
    }

    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "running" once it is running.
     */
    semp_real(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */
    for (i = 0; i < DISK_UNITS; i++) 
    {
        sprintf(termbuf, "%d", i); //Michael - I changed buf to termbuf, since termbuf is defined
        sprintf(name, "DiskDriver%d", i);
        diskpids[i] = fork1(name, DiskDriver, termbuf, USLOSS_MIN_STACK, 2); //Michael - ^^ same here
        if (diskpids[i] < 0) 
        {
           console("start3(): Can't create disk driver %d\n", i);
           halt(1);
        }
    }
    semp_real(running);
    semp_real(running);

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case names.
     */
    pid = spawn_real("start4", start4, NULL,  8 * USLOSS_MIN_STACK, 3);
    pid = wait_real(&status);

    /* Zap the device drivers */
    zap(clockPID);  // clock driver
    join(&status); /* for the Clock Driver */
    for (i = 0; i < DISK_UNITS; i++)
    {
        /*
         *Don’t zap disk drivers directly.  
         *You could use start3 to indicate the intention to “zap” a disk driver.  
         *If the disk driver “sees” the intention, it should quit.
         *Potentially add a mailbox for the disk drivers for this purpose
         *Could send a message to them as the intent to zap
         *Once they receive the message, they could quit
         */

        //can change global ZAP_FLAG to 1 to alert disk drivers to zap intention.
        ZAP_FLAG = 1;

        //join will cause start3 to wait for disk driver to quit
        join(&status);
    }

    //redundancy
    for (int i = 0; i < 3; i++)
    {
        quit(0);
    }
    quit(0); 
    return 0;
} /* start3 */


/* ClockDriver */
static int
ClockDriver(char *arg)
{
    int result;
    int status;

    //check if zapped, potentially quit(0)
    
    /*
     * Let the parent know we are running and enable interrupts.
     */
    semv_real(running);
    psr_set(psr_get() | PSR_CURRENT_INT);
    while(! is_zapped()) 
    {
	    result = waitdevice(CLOCK_DEV, 0, &status);
	    if (result != 0) 
        {
	        return 0;
	    }
	    /*
	    * Compute the current time and wake up any processes
	    * whose time has come.
	    */
        //check the SleepQ somehow (one at a time, or check the whole list?)
        //compare the current sysclock time with the procs bedtime
        //if result is >= wake_time, wake the process
        //wake the process by doing a semv_real on the proc's private_sem
        //also remove the proc from the SleepQ (need some function for this)
        //also the SleepQ should be protected using sleep_semaphore
    }

    //once out of while loop, quit(0) for start3's join
    quit(0);
} /* ClockDriver */


/* DiskDriver */
static int
DiskDriver(char *arg)
{
    int unit = atoi(arg);
    device_request my_request;
    int result;
    int status;

    //somewhere do a semv_real(running);

    driver_proc_ptr current_req;

    if (DEBUG4 && debugflag4)
    {
        console("DiskDriver(%d): started\n", unit);
    }

    /* Get the number of tracks for this disk */
    my_request.opr  = DISK_TRACKS;
    my_request.reg1 = &num_tracks[unit];

    result = device_output(DISK_DEV, unit, &my_request);

    if (result != DEV_OK) 
    {
        console("DiskDriver (%d): did not get DEV_OK on DISK_TRACKS call\n", unit);
        console("DiskDriver (%d): is the file disk%d present???\n", unit, unit);
        halt(1);
    }

    waitdevice(DISK_DEV, unit, &status);
    if (DEBUG4 && debugflag4)
    {
        console("DiskDriver(%d): tracks = %d\n", unit, num_tracks[unit]);
    }

    //while loop (check if zapped). can check the ZAP_FLAG global int
    //wake up user level process from private mbox/sem and give data
    while (ZAP_FLAG != 1)
    {
        //do more stuff 
    }

    //quit after giving user-level process the data
    quit(0);
} /* DiskDriver */


/* sleep_first */
static void 
sleep_first(sysargs *args_ptr)
{
    int seconds;
    int result;

    seconds = (int) args_ptr->arg1;
    //check validity of seconds
    //result = -1 if illegal argument

    result = sleep_real(seconds);
    if (result == -1)
    {
        console("sleep_first(): sleep_real returned -1, illegal values.\n");
    }
    args_ptr->arg4 = (void *) result;

    return;
} /* sleep_first */


/* sleep_real */
int 
sleep_real(int seconds)
{
    //attempt to enter the critical region
    semp_real(sleep_semaphore);

    driver_proc_ptr current_proc;
    current_proc = &Driver_Table[getpid() % MAXPROC];

    //put process onto the sleep queue
    sleep_number = insert_sleep_q(current_proc);

    //record the time it was put to sleep
    current_proc->bedtime = sys_clock();

    //record amount of seconds to sleep as microseconds
    current_proc->wake_time = seconds * 1000000;

    //leave the critical region
    semv_real(sleep_semaphore);

    //block the process possibly with sem/mutex/mailboxreceive
    semp_real(current_proc->private_sem);

    return 0;
} /* sleep_real */


/* disk_read_first */
static void 
disk_read_first(sysargs *args_ptr)
{
    int unit;
    int track;
    int first;
    int sectors;
    void *buffer;
    int status; //inspect how status is applied
    int result;

    buffer = args_ptr->arg1;
    sectors = (int) args_ptr->arg2;
    track = (int) args_ptr->arg3;
    first = (int) args_ptr->arg4;
    unit = (int) args_ptr->arg5;
    //check validity of arguments

    result = disk_read_real(unit, track, first, sectors, buffer);
    if (result == -1)
    {
        console("disk_read_first(): disk_read_real returned -1, illegal values.\n");
    }
    args_ptr->arg1 = (void *) status; //further inspection of status required, arg1 should be 0 or disk status register
    args_ptr->arg4 = (void *) result;
    return;
} /* disk_read_first */


/* disk_read_real */
int 
disk_read_real(int unit, int track, int first, int sectors, void *buffer)
{
    return 0;
} /* disk_read_real */


/* disk_write_first */
static void 
disk_write_first(sysargs *args_ptr)
{
    int unit;
    int track;
    int first;
    int sectors;
    void *buffer;
    int status; //inspect how status is applied
    int result;

    buffer = args_ptr->arg1;
    sectors = (int) args_ptr->arg2;
    track = (int) args_ptr->arg3;
    first = (int) args_ptr->arg4;
    unit = (int) args_ptr->arg5;
    //check validity of arguments

    result = disk_write_real(unit, track, first, sectors, buffer);
    if (result == -1)
    {
        console("disk_write_first(): disk_write_real returned -1, illegal values.\n");
    }
    args_ptr->arg1 = (void *) status; //further inspection of status required, arg1 should be 0 or disk status register
    args_ptr->arg4 = (void *) result;
    return;
} /* disk_write_first */



/* disk_write_real */
int 
disk_write_real(int unit, int track, int first, int sectors, void *buffer)
{
    return 0;
} /* disk_write_real */


/* disk_size_first */
static void 
disk_size_first(sysargs *args_ptr)
{
    int unit;
    int sector;
    int track;
    int disk;
    int result;

    unit = (int) args_ptr->arg1;
    //check validity of unit

    result = disk_size_real(unit, &sector, &track, &disk);
    if (result == -1)
    {
        console("disk_size_first(): disk_size_real returned -1, illegal values\n");
    }
    args_ptr->arg1 = (void *) sector;
    args_ptr->arg2 = (void *) track;
    args_ptr->arg3 = (void *) disk;
    args_ptr->arg4 = (void *) result;
    return;
} /* disk_size_first */


/* disk_size_real */
int 
disk_size_real(int unit, int *sector, int *track, int *disk)
{
    return 0;
} /* disk_size_real */



/* insert_sleep_q */
int 
insert_sleep_q(driver_proc_ptr proc_ptr)
{
    int num_sleep_procs = 0;
    driver_proc_ptr walker, previous;
    previous = NULL;
    walker = SleepQ;
    
    if (walker == NULL) 
    {
        /* process goes at front of SleepQ */
        SleepQ = proc_ptr;
        num_sleep_procs++;
    }
    else 
    {
        num_sleep_procs++; //starts at 1
        while (walker->next_ptr != NULL) //counts how many are in Q already
        {
            num_sleep_procs++;
            walker = walker->next_ptr;
        }
        walker->next_ptr = proc_ptr; //inserts proc to end of Q
        num_sleep_procs++; //counts the insert
    }

    return num_sleep_procs;
} /* insert_sleep_q */