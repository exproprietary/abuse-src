/*************************************
  Threaded sound driver system
**************************************/

// comminicates with sound sub-system via shared memory

#include "macs.hpp"
#include "sound.hpp"
#include "readwav.hpp"
#include "dprint.hpp"
#include "jmalloc.hpp"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "timing.hpp"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <unistd.h>

enum { SFXCMD_QUIT,
       SFXCMD_REGISTER,
       SFXCMD_UNREGISTER,
       SFXCMD_PLAY
     };

#define DIN_NAME  "/tmp/sfxdrv.signal"
#define DOUT_NAME "/tmp/sfxdrv.command"

class sfx_handle
{
  public :
  int handle;
  sound_effect *me;
  sfx_handle   *next;  
  sfx_handle(sound_effect *Me, sfx_handle *Next, int Handle)
  { me=Me;
    next=Next;
    handle=Handle;
  }
} ;

static sfx_handle *sfx_list=NULL;
static ushort last_allocated_sfx_id=0;
static int sfx_driver_out_fd=-1,sfx_driver_in_fd=-1;
static int sdriver_pid=0;

static int sfx_shm_id;
static void *sfx_shm_addr;
static int sfx_end;

static void kill_sound_driver()
{
  if (sfx_driver_out_fd>=0)
  {
    uchar cmd=SFXCMD_QUIT;        // failed, tell the driver good-bye
    write(sfx_driver_out_fd,&cmd,1);
    close(sfx_driver_out_fd);
    close(sfx_driver_in_fd);
    sfx_driver_out_fd=-1;

    milli_wait(10);
    kill(sdriver_pid,SIGUSR1);
    unlink(DIN_NAME);
    unlink(DOUT_NAME);
		shmdt(sfx_shm_addr);
  }
}



#ifdef __sgi
static void sfx_clean_up(...)
#else
static void sfx_clean_up(int why)      // on exit unattach all shared memory links
#endif
{  
  while (sfx_list)
  {
    sfx_handle *last=sfx_list;
    sfx_list=sfx_list->next;
    jfree(last);
  }
}



#define TOTAL_SIGS 29

int sigs[TOTAL_SIGS]={SIGHUP,SIGINT,SIGQUIT,SIGILL,SIGTRAP,
		      SIGABRT,SIGIOT,SIGBUS,SIGFPE,SIGKILL,
		      SIGUSR1,SIGSEGV,SIGUSR2,SIGPIPE,SIGALRM,
		      SIGTERM,SIGCHLD,SIGCONT,SIGSTOP,
		      SIGTSTP,SIGTTIN,SIGTTOU,SIGIO,
		      SIGURG,SIGXCPU,SIGXFSZ,SIGVTALRM,SIGPROF,
		      SIGWINCH};

#include <errno.h>

sound_effect::sound_effect(char *filename)
{
	int handle;

  if (sfx_driver_out_fd < 0) return;

	long sample_speed;
	void *dat=read_wav(filename,sample_speed,size); 

	handle = sfx_end;
	memcpy(sfx_shm_addr + handle, dat, size);
	jfree(dat);
	sfx_end += size;

	int fail=0;
	uchar cmd=SFXCMD_REGISTER;

	if (write(sfx_driver_out_fd,&cmd,1)==0)
		fail=1;
	else if (write(sfx_driver_out_fd,&handle,sizeof(handle))!=sizeof(handle))
		fail=1;
	else if (write(sfx_driver_out_fd,&size,sizeof(size))!=sizeof(size))
		fail=1;
	else if (read(sfx_driver_in_fd,&cmd,1)!=1)
		fail=1;
	else if (cmd!=1)    // did we get an 'OK' back so when can delete the shm ID?
		fail=1;

	sfx_list=new sfx_handle(this,sfx_list,handle);	
	data=(void *)sfx_list;

}


sound_effect::~sound_effect()
{
}


void sound_effect::play(int volume, int pitch, int panpot)
{
  if (sfx_driver_out_fd>=0 && data)
  {   
    sfx_handle *h=(sfx_handle *)data;
    uchar cmd=SFXCMD_PLAY;
    int fail=0;
    if (write(sfx_driver_out_fd,&cmd,1)!=1)
      fail=1;
    else if (write(sfx_driver_out_fd,&h->handle,sizeof(h->handle))!=sizeof(h->handle))
      fail=1;
    else if (write(sfx_driver_out_fd,&volume,sizeof(volume))!=sizeof(volume))
       fail=1;

    if (fail) 
       kill_sound_driver();
  }
}


int sound_init(int argc, char **argv) 
{
  int i;

  for (i=1;i<argc;i++)
  {
    if (!strcmp(argv[i],"-nosound"))
    {
      dprintf("sound : disabled with (-nosound)\n");
      sfx_driver_out_fd=-1;
      return 0;
    }
  }

#if defined(__linux__)
  FILE *sfx_driver_fp=popen("lnx_sdrv","r");
#elif defined(_AIX)
  FILE *sfx_driver_fp=popen("/usr/bin/run_ums aix_sdrv","r");
#else
  FILE *sfx_driver_fp=popen("sgi_sdrv","r");
#endif

  if (!sfx_driver_fp)
  {
    dprintf("Error starting sound effect, could not run sfx driver\n"
	    "make sure it is in your path and you execute permission\n");
    sfx_driver_out_fd=-1;
    return 0;
  }

  char str[100];
  if (!fgets(str,100,sfx_driver_fp) || !sscanf(str,"%d",&sdriver_pid) || sdriver_pid<0)
  {
//    pclose(sfx_driver_fp);
    dprintf("sound effects driver returned failure, sound effects disabled\n");
 //   pclose(sfx_driver_fp);
    sfx_driver_out_fd=-1;
    return 0;
  } else
  {
//    pclose(sfx_driver_fp);

    do
    { milli_wait(50);
    } while (access(DIN_NAME,R_OK)); 
    sfx_driver_in_fd=open(DIN_NAME,O_RDWR);

    do
    { milli_wait(50);
    } while (access(DOUT_NAME,W_OK)); 
    fprintf(stderr,"opening %s for writing\n",DOUT_NAME);
    sfx_driver_out_fd=open(DOUT_NAME,O_RDWR);

// create large (3Mb) shared mem segment for storing all sfx
		sfx_shm_id=shmget(IPC_PRIVATE,3*1024*1024,IPC_CREAT | 0777);
		sfx_shm_addr=shmat(sfx_shm_id,NULL,0);

		if (write(sfx_driver_out_fd, &sfx_shm_id, sizeof(sfx_shm_id))!=sizeof(sfx_shm_id))
		{
			dprintf("can't talk with sdrv\n");
			return 0;
		}
		char cmd;
		read(sfx_driver_in_fd, &cmd, 1);
		shmctl(sfx_shm_id,IPC_RMID,NULL);

    for (i=0;i<TOTAL_SIGS;i++)
       signal(sigs[i],sfx_clean_up);

    atexit(sound_uninit);
  }
  
  return SFX_INITIALIZED;
}


void sound_uninit()
{
  if (sfx_driver_out_fd>=0)
  {
    if (sfx_list)
    {
#ifdef __sgi
      sfx_clean_up();
#else
      sfx_clean_up(1);
#endif
    }
   
    uchar cmd;
    cmd=SFXCMD_QUIT;
    write(sfx_driver_out_fd,&cmd,1);   // send quit commmand to the driver
    close(sfx_driver_out_fd);          // close connection
    close(sfx_driver_in_fd);          // close connection
    sfx_driver_out_fd=-1;
    sfx_driver_in_fd=-1;
  }
}






song::song(char *filename) { ; }

void song::play(unsigned char volume) { ; }
int song::playing() { return 0; }
void song::set_volume(int vol)  { ; }
void song::stop(long fadeout_time) { ;  }
int playing() { return 0; }
song::~song() { ; }



