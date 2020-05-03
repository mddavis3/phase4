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
int remove_sleep_q(driver_proc_ptr);

int insert_disk_q(driver_proc_ptr);
int remove_disk_q(driver_proc_ptr);

void print_sems(void);

/* -------------------------- Globals ------------------------------------- */

static int debugflag4 = 0;

static int running; /*semaphore to synchronize drivers and start3*/

static struct driver_proc Driver_Table[MAXPROC];

static int diskpids[DISK_UNITS];

static int num_tracks[DISK_UNITS];

struct driver_proc dummy_proc = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

int ZAP_FLAG = 0;

driver_proc_ptr SleepQ = NULL;

int sleep_number = 0;

driver_proc_ptr DQ = NULL;

int DQ_number = 0;

static int sleep_semaphore;

static int disk_semaphore;

static int DQ_semaphore;

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
    if (DEBUG4 && debugflag4)
    {
        console ("start3(): sys_vec assignment.\n");
    }
    sys_vec[SYS_SLEEP] = sleep_first;
    sys_vec[SYS_DISKREAD] = disk_read_first;
    sys_vec[SYS_DISKWRITE] = disk_write_first;
    sys_vec[SYS_DISKSIZE] = disk_size_first;
    
    /* Initialize the phase 4 process table */
    if (DEBUG4 && debugflag4)
    {
        console ("start3(): process table.\n");
    }
    for (int i = 0; i < MAXPROC; i++)
    {
        Driver_Table[i] = dummy_proc;
        Driver_Table[i].private_sem = semcreate_real(0);
    }

    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    if (DEBUG4 && debugflag4)
    {
        console ("start3(): create semaphores.\n");
    }
    sleep_semaphore = semcreate_real(1);
    disk_semaphore = semcreate_real(0);
    DQ_semaphore = semcreate_real(1);
    running = semcreate_real(0);

    if (DEBUG4 && debugflag4)
    {
        print_sems();
        console ("start3(): fork clock driver.\n");
    }
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
    if (DEBUG4 && debugflag4)
    {
        console ("start3(): semp on running sem.\n");
    }
    semp_real(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */
    if (DEBUG4 && debugflag4)
    {
        console ("start3(): fork drisk drivers.\n");
    }
    for (i = 0; i < DISK_UNITS; i++) 
    {
        sprintf(termbuf, "%d", i); 
        sprintf(name, "DiskDriver%d", i);
        diskpids[i] = fork1(name, DiskDriver, termbuf, USLOSS_MIN_STACK, 2);
        if (diskpids[i] < 0) 
        {
           console("start3(): Can't create disk driver %d\n", i);
           halt(1);
        }
    }

    if (DEBUG4 && debugflag4)
    {
        console ("start3(): semp on running sem x2.\n");
    }
    semp_real(running);     //forces start3() to wait until first diskdriver is spawned and setup 
    semp_real(running);     //forces start3() to wait until second diskdriver is spawned and setup

    if (DEBUG4 && debugflag4)
    {
        print_sems();   //print sems to see where they are
    }

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case names.
     */
    if (DEBUG4 && debugflag4)
    {
        console ("start3(): spawn start4 and wait on it.\n");
    }
    pid = spawn_real("start4", start4, NULL,  8 * USLOSS_MIN_STACK, 3);
    pid = wait_real(&status);

    /* Zap the device drivers */
    if (DEBUG4 && debugflag4)
    {
        console ("start3(): zap and join clock driver.\n");
    }
    zap(clockPID);  // clock driver
    join(&status); /* for the Clock Driver */

    if (DEBUG4 && debugflag4)
    {
        console ("start3(): 'zap' and join disk drivers\n");
    }
    for (i = 0; i < DISK_UNITS; i++)
    {
        //alert disk drivers to zap intention.
        ZAP_FLAG = 1;

        //join will cause start3 to wait for disk driver to quit
        if (DEBUG4 && debugflag4)
        {
            console ("start3(): semv disk_semaphore\n");
            print_sems();
        }
        semv_real(disk_semaphore);

        if (DEBUG4 && debugflag4)
        {
            console ("start3(): join\n");
        }
        join(&status);
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
    int current_time;
    driver_proc_ptr proc_ptr, proc_to_wake;

    /*
     * Let the parent know we are running and enable interrupts.
     */
    if (DEBUG4 && debugflag4)
    {
        console ("ClockDriver(): semv on running semaphore\n");
    }
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
        current_time = sys_clock();
        proc_ptr = SleepQ;

        while (proc_ptr != NULL)
        {
            proc_to_wake = proc_ptr;
            proc_ptr = proc_ptr->next_ptr;

            //compare current time with procs wake_time
            if (current_time >= proc_to_wake->wake_time)
            {  
                //remove the proc from the SleepQ 
                if (DEBUG4 && debugflag4)
                {
                    console ("ClockDriver(): removing proc from SleepQ\n");
                }
                sleep_number = remove_sleep_q(proc_to_wake);

                //wake the process
                if (DEBUG4 && debugflag4)
                {
                    console ("ClockDriver(): waking proc\n");
                }
                semv_real(proc_to_wake->private_sem);    
            } 
        }
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

    /*
     * Let the parent know we are running and enable interrupts.
     */
    if (DEBUG4 && debugflag4)
    {
        console ("DiskDriver(%d): semv on running semaphore\n", unit);
    }

    semv_real(running);
    psr_set(psr_get() | PSR_CURRENT_INT);

    driver_proc_ptr current_req;

    if (DEBUG4 && debugflag4)
    {
        console("DiskDriver(%d): started\n", unit);
    }

    /* Get the number of tracks for this disk */
    if (DEBUG4 && debugflag4)
    {
        console ("DiskDriver(%d): getting # of tracks for disk\n", unit);
    }
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

    //allow DiskDriver(1) to finish getting # of tracks
    if (unit == 0)
    {
        //down on running semaphore
        semp_real(running);
    }
    else
    {
        //up on running semaphore
        semv_real(running);
    }
    
    if (DEBUG4 && debugflag4)
    {
        console ("DiskDriver(%d): enter while loop till zapped\n", unit);
    }
    while (ZAP_FLAG != 1)
    {
        //block on semaphore to await request from user process
        if (DEBUG4 && debugflag4)
        {
            console ("DiskDriver(%d): semp on disk semaphore to wait for requests\n", unit);
        }
        semp_real(disk_semaphore);

        //take the next request on the DiskQ
        if (DEBUG4 && debugflag4)
        {
            console ("DiskDriver(%d): setting current_req = DQ\n", unit);
        }
        current_req = DQ;
            
        if (current_req != NULL) //make sure current_req is not NULL to avoid segmentation fault
        {
            //check if a disk_size request to skip read/write stuff
            if (DEBUG4 && debugflag4)
            {
                console ("DiskDriver(%d): checking current_req operation type\n", unit);
            }
            if (current_req->operation == DISK_SIZE)
            {
                //enter critical region
                if (DEBUG4 && debugflag4)
                {
                    console ("DiskDriver(%d): semp on dq_sem to enter critical region\n", unit);
                }
                semp_real(DQ_semaphore);

                DQ_number = remove_disk_q(current_req); //remove current_req from DQ
            
                //leave critical region
                if (DEBUG4 && debugflag4)
                {
                    console ("DiskDriver(%d): semv on DQ_sem to leave critical region\n", unit);
                }
                semv_real(DQ_semaphore);

                //wake up user on private sem
                if (DEBUG4 && debugflag4)
                {
                    console ("DiskDriver(%d): semv on private_sem to unblock proc\n", unit);
                }
                semv_real(current_req->private_sem);
            }
            else
            {
                //enter critical region
                if (DEBUG4 && debugflag4)
                {
                    console ("DiskDriver(%d): semp on dq_sem to enter critical region\n", unit);
                }
                semp_real(DQ_semaphore);

                DQ_number = remove_disk_q(current_req); //remove current_req from DQ

                //read/write loop to get data to/from disk
                //disk seek request to move arm to right track (unless we store arm position in global (int?) and know its there already)
                //update track and sector #s if reading across track boundaries (code to do this in slides)
                //
                //wake up user on private sem and give data

                //disk seek first to adjust arm 
                my_request.opr  = DISK_SEEK;
                my_request.reg1 = current_req->track_start;
                result = device_output(DISK_DEV, current_req->unit, &my_request);

                if (result != DEV_OK) 
                {
                    console("DiskDriver (%d): did not get DEV_OK on DISK_SEEK call\n", unit);
                    console("DiskDriver (%d): is the file disk%d present???\n", unit, current_req->unit);
                    halt(1);
                }

                waitdevice(DISK_DEV, current_req->unit, &status);

                //do read/write op
                my_request.opr = current_req->operation;
                my_request.reg1 = current_req->sector_start;
                my_request.reg2 = current_req->disk_buf;
                result = device_output(DISK_DEV, current_req->unit, &my_request);

                if (result != DEV_OK) 
                {
                    console("DiskDriver (%d): did not get DEV_OK on %d call\n", unit, current_req->operation);
                    console("DiskDriver (%d): is the file disk%d present???\n", unit, current_req->unit);
                    halt(1);
                }

                waitdevice(DISK_DEV, current_req->unit, &status);
                current_req->status = status;

                //leave critical region
                if (DEBUG4 && debugflag4)
                {
                    console ("DiskDriver(%d): semv on DQ_sem to leave critical region\n", unit);
                }
                semv_real(DQ_semaphore);

                //wake up user on private sem
                if (DEBUG4 && debugflag4)
                {
                    console ("DiskDriver(%d): semv on private_sem to unblock proc\n", unit);
                }
                semv_real(current_req->private_sem);
            }
        }    
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
    if (seconds < 0)
    {
        result = -1;
        console("sleep_first(): illegal value, seconds < 0.\n");
        args_ptr->arg4 = (void *) result;
        return;
    }

    if (DEBUG4 && debugflag4)
    {
        console ("sleep_first(): calling sleep_real\n");
    }
    result = sleep_real(seconds);
    if (result == -1)
    {
        console("sleep_first(): sleep_real returned -1, illegal value.\n");
    }

    if (DEBUG4 && debugflag4)
    {
        console ("sleep_first(): sleep_real returned\n");
    }
    args_ptr->arg4 = (void *) result;

    return;
} /* sleep_first */


/* sleep_real */
int 
sleep_real(int seconds)
{
    //attempt to enter the critical region
    if (DEBUG4 && debugflag4)
    {
        console ("sleep_real(): call semp on sleep_sem\n");
    }
    semp_real(sleep_semaphore);

    driver_proc_ptr current_proc;
    current_proc = &Driver_Table[getpid() % MAXPROC];

    //put process onto the sleep queue
    if (DEBUG4 && debugflag4)
    {
        console ("sleep_real(): call insert_sleep_q\n");
    }
    sleep_number = insert_sleep_q(current_proc);

    //record the time it was put to sleep
    current_proc->bedtime = sys_clock();

    //record amount of seconds to sleep as microseconds
    //add bedtime to get the time to wake up
    current_proc->wake_time = (seconds * 1000000) + current_proc->bedtime;

    //leave the critical region
    if (DEBUG4 && debugflag4)
    {
        console ("sleep_real(): call semv on sleep_sem\n");
    }
    semv_real(sleep_semaphore);

    //block the process possibly with sem/mutex/mailboxreceive
    if (DEBUG4 && debugflag4)
    {
        console ("sleep_real(): call semp on proc's private sem to block it\n");
    }
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
    int status;
    int result = 0;

    buffer = args_ptr->arg1;
    sectors = (int) args_ptr->arg2;
    track = (int) args_ptr->arg3;
    first = (int) args_ptr->arg4;
    unit = (int) args_ptr->arg5;

    //check validity of arguments

    if (result == -1)
    {
        console("disk_read_first(): illegal values given, result == -1.\n");
        //return
    }

    status = disk_read_real(unit, track, first, sectors, buffer);
    
    args_ptr->arg1 = (void *) status;
    args_ptr->arg4 = (void *) result;
    return;
} /* disk_read_first */


/* disk_read_real */
int 
disk_read_real(int unit, int track, int first, int sectors, void *buffer)
{
    //attempt to enter the critical region
    if (DEBUG4 && debugflag4)
    {
        console ("disk_read_real(): call semp on DQ_sem\n");
    }
    semp_real(DQ_semaphore);

    //initialize current_proc
    driver_proc_ptr current_proc;
    current_proc = &Driver_Table[getpid() % MAXPROC];

    //pack the request
    current_proc->operation = DISK_READ;
    current_proc->unit = unit; //which disk to read
    current_proc->track_start = track; //starting track
    current_proc->sector_start = first; //starting sector
    current_proc->num_sectors = sectors; //number of sectors
    current_proc->disk_buf = buffer; //data buffer

    //put request on the DQ
    DQ_number = insert_disk_q(current_proc);

    //leave the critical region
    semv_real(DQ_semaphore);

    //alert Disk Driver there's an entry in DQ
    semv_real(disk_semaphore);

    //wait for Disk Driver to complete operation
    if (DEBUG4 && debugflag4)
    {
        console ("disk_read_real(): call semp on proc's private sem to block it\n");
    }
    semp_real(current_proc->private_sem);

    //return results - disk status register if transfer unsuccessful, 0 if successful
    //disk status register is grabbed using a call device_input(DISK_DEV, unit, &status)
    //output is stored in status
    //return status
    if (current_proc->status == 0)
    {
        console("disk_read_real(): status returned as 0\n");
    }
    else
    {
        console("disk_read_real(): status returned as &d\n", current_proc->status);
    }
    
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
    int status; 
    int result = 0;

    buffer = args_ptr->arg1;
    sectors = (int) args_ptr->arg2;
    track = (int) args_ptr->arg3;
    first = (int) args_ptr->arg4;
    unit = (int) args_ptr->arg5;

    //check validity of arguments

    if (result == -1)
    {
        console("disk_write_first(): disk_write_real returned -1, illegal values.\n");
        //return
    }

    status = disk_write_real(unit, track, first, sectors, buffer);
    
    args_ptr->arg1 = (void *) status; 
    args_ptr->arg4 = (void *) result;
    return;
} /* disk_write_first */



/* disk_write_real */
int 
disk_write_real(int unit, int track, int first, int sectors, void *buffer)
{
    //attempt to enter the critical region
    if (DEBUG4 && debugflag4)
    {
        console ("disk_write_real(): call semp on DQ_sem\n");
    }
    semp_real(DQ_semaphore);

    //initialize current_proc
    driver_proc_ptr current_proc;
    current_proc = &Driver_Table[getpid() % MAXPROC];

    //pack the request
    current_proc->operation = DISK_WRITE;
    current_proc->unit = unit; //which disk to write to
    current_proc->track_start = track; //starting track
    current_proc->sector_start = first; //starting sector
    current_proc->num_sectors = sectors; //number of sectors
    current_proc->disk_buf = buffer; //data buffer

    //put request on the DQ
    DQ_number = insert_disk_q(current_proc);

    //leave the critical region
    semv_real(DQ_semaphore);

    //alert Disk Driver there's an entry in DQ
    semv_real(disk_semaphore);

    //wait for Disk Driver to complete operation
    if (DEBUG4 && debugflag4)
    {
        console ("disk_write_real(): call semp on proc's private sem to block it\n");
    }
    semp_real(current_proc->private_sem);

    //return results - disk status register if transfer unsuccessful, 0 if successful
    //disk status register is grabbed using a call device input(DISK_DEV, unit,&status)
    //output is stored in status
    //return status
    if (current_proc->status == 0)
    {
        console("disk_write_real(): status returned as 0\n");
    }
    else
    {
        console("disk_write_real(): status returned as &d\n", current_proc->status);
    }
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
    if (unit < 0 || unit > 1)
    {
        result = -1;
        console("disk_size_first(): illegal value, unit < 0 or > 1.\n");
        args_ptr->arg4 = (void *) result;
        return;
    }

    result = disk_size_real(unit, &sector, &track, &disk);

    if (DEBUG4 && debugflag4)
    {
        console ("disk_size_first(): after _real, sector = %d, track = %d, disk = %d\n", sector, track, disk);
    }

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

    //attempt to enter the critical region
    if (DEBUG4 && debugflag4)
    {
        console ("disk_size_real(): call semp on DQ_sem\n");
    }
    semp_real(DQ_semaphore);

    driver_proc_ptr current_proc;
    current_proc = &Driver_Table[getpid() % MAXPROC];
    current_proc->operation = DISK_SIZE;
    DQ_number = insert_disk_q(current_proc);

    //leave the critical region
    semv_real(DQ_semaphore);

    //alert Disk Driver there's an entry in DQ
    semv_real(disk_semaphore);

    //wait for Disk Driver to complete operation
    if (DEBUG4 && debugflag4)
    {
        console ("disk_size_real(): call semp on proc's private sem to block it\n");
    }
    semp_real(current_proc->private_sem);

    //assign values and return
    *sector = DISK_SECTOR_SIZE;
    *track = DISK_TRACK_SIZE;
    *disk = num_tracks[unit];

    if (DEBUG4 && debugflag4)
    {
        console ("disk_size_real(): values after - sector = %d, track = %d, disk = %d\n", *sector, *track, *disk);
    }

    return 0;
} /* disk_size_real */



/* insert_sleep_q */
int 
insert_sleep_q(driver_proc_ptr proc_ptr)
{
    int num_sleep_procs = 0;
    driver_proc_ptr walker;
    walker = SleepQ;

    if (walker == NULL) 
    {
        /* process goes at front of SleepQ */
        if (DEBUG4 && debugflag4)
        {
            console ("insert_sleep_q(): SleepQ was empty, now has 1 entry\n");
        }
        SleepQ = proc_ptr;
        num_sleep_procs++;
    }
    else 
    {
        if (DEBUG4 && debugflag4)
        {
            console ("insert_sleep_q():SleepQ wasn't empty, should have >1\n");
        }
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



/* remove_sleep_q */
int
remove_sleep_q(driver_proc_ptr proc_ptr)
{
    int num_sleep_procs = sleep_number;
    driver_proc_ptr walker, previous;
    walker = SleepQ;

    //protect the SleepQ with a semaphore
    //enter critical region
    if (DEBUG4 && debugflag4)
    {
        console ("remove_sleep_q(): semp on sleep_sem\n");
    }
    semp_real(sleep_semaphore);

    //if SleepQ is empty
    if(num_sleep_procs == 0)
    {
        console("remove_sleep_q(): SleepQ empty. Return.\n");
    }
    
    //elseif SleepQ has one entry
    else if (num_sleep_procs == 1)
    {
        if (DEBUG4 && debugflag4)
        {
            console ("remove_sleep_q(): SleepQ had 1 entry, now 0\n");
        }
        SleepQ = NULL;
        num_sleep_procs--;
    }
    
    //else SleepQ has > 1 entry
    else
    {
        if (DEBUG4 && debugflag4)
        {
            console ("remove_sleep_q(): SleepQ had >1 entry\n");
        }
        if (SleepQ == proc_ptr) //1st entry to be removed
        {
            SleepQ = walker->next_ptr;
            proc_ptr->next_ptr = NULL;
            num_sleep_procs--;
        }
        else //2nd entry or later to be removed
        {
            while (walker != proc_ptr)
            {
                previous = walker;
                walker = walker->next_ptr;
            }
            
            previous->next_ptr = walker->next_ptr;
            walker->next_ptr = NULL;
            num_sleep_procs--;
        }   
    }

    //leave critical region
    if (DEBUG4 && debugflag4)
    {
        console ("remove_sleep_q(): semv on sleep_sem\n");
    }
    semv_real(sleep_semaphore);

    return num_sleep_procs;
} /* remove_sleep_q */



/* insert_disk_q */
int 
insert_disk_q(driver_proc_ptr proc_ptr)
{
    int num_disk_procs = 0;
    driver_proc_ptr walker;
    walker = DQ;

    if (walker == NULL) 
    {
        /* process goes at front of DQ */
        if (DEBUG4 && debugflag4)
        {
            console ("insert_disk_q(): DQ was empty, now has 1 entry\n");
        }
        DQ = proc_ptr;
        num_disk_procs++;
    }
    else 
    {
        if (DEBUG4 && debugflag4)
        {
            console ("insert_disk_q():DQ wasn't empty, should have >1\n");
        }
        num_disk_procs++; //starts at 1
        while (walker->next_dq_ptr != NULL) //counts how many are in Q already
        {
            num_disk_procs++;
            walker = walker->next_dq_ptr;
        }
        walker->next_dq_ptr = proc_ptr; //inserts proc to end of Q
        num_disk_procs++; //counts the insert
    }

    return num_disk_procs;
} /* insert_disk_q */



/* remove_disk_q */
int 
remove_disk_q(driver_proc_ptr proc_ptr)
{
    int num_disk_procs = DQ_number;
    driver_proc_ptr walker, previous;
    walker = DQ;

    //if DQ is empty
    if(num_disk_procs == 0)
    {
        console("remove_disk_q(): DQ empty. Return.\n");
    }
    
    //elseif DQ has one entry
    else if (num_disk_procs == 1)
    {
        if (DEBUG4 && debugflag4)
        {
            console ("remove_disk_q(): DQ had 1 entry, now 0\n");
        }
        DQ = NULL;
        num_disk_procs--;
    }
    
    //else DQ has > 1 entry
    else
    {
        if (DEBUG4 && debugflag4)
        {
            console ("remove_disk_q(): DQ had >1 entry\n");
        }
        DQ = walker->next_dq_ptr;
        proc_ptr->next_dq_ptr = NULL;
        num_disk_procs--;
    }

    return num_disk_procs;
} /* remove_disk_q */


/* print_sems */
void 
print_sems(void)
{
    //for debug purposes.
    console("\nrunning semaphore: %d, disk_semaphore: %d, DQ_semaphore %d, sleep_semaphore: %d\n\n", running, disk_semaphore, DQ_semaphore, sleep_semaphore);
    return;
} /* print_sems*/