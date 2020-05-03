#define DEBUG4 1
#define ZAP_SIGNAL 1
#define DISK_SIZE 4

typedef struct driver_proc * driver_proc_ptr;

struct driver_proc 
{
   driver_proc_ptr next_ptr;
   driver_proc_ptr next_dq_ptr;

   int   wake_time;    /* for sleep syscall */
   int   been_zapped;


   /* Used for disk requests */
   int   operation;    /* DISK_READ, DISK_WRITE, DISK_SEEK, DISK_TRACKS */
   int   unit;
   int   track_start;
   int   sector_start;
   int   num_sectors;
   void  *disk_buf;

   //more fields to add
   int   bedtime;
   int   private_sem;

};

