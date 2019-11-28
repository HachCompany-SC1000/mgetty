#ident "$Id: mgetty.c,v 4.35 2002/12/05 11:54:03 gert Exp $ Copyright (c) Gert Doering"

/* mgetty.c
 *
 * mgetty main module - initialize modem, lock, get log name, call login
 *
 * some parts of the code (lock handling, writing of the utmp entry)
 * are based on the "getty kit 2.0" by Paul Sutcliffe, Jr.,
 * paul@devon.lns.pa.us, and are used with permission here.
 */
char Teststring[]="Hallo, hier ist der SC1000";
#include <stdio.h>
#include "syslibs.h"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/times.h>

#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>

#include <sys/stat.h>
#include <signal.h>

#include "version.h"
#include "mgetty.h"
#include "policy.h"
#include "tio.h"
#include "fax_lib.h"
#include "mg_utmp.h"

#include "config.h"
#include "conf_mg.h"
#include <sys/ioctl.h>
#include "ttyResource.h"

// libMbManager
#ifdef __SH3__
#include "../SC1000/include/BusDeviceLib.h"
#include "../SC1000/include/UiManager.h"
#else
#include "../include/BusDeviceLib.h"
#include "../include/UiManager.h"
#endif

#ifdef VOICE
#include "voice/include/voice.h"
#endif

//#define SC1000
#define GSM_READY 		(1 << 0)
#define GSM_CALL  		(1 << 1)
#define GSM_CONNECTION 		(1 << 2)
#define GSM_PIN_ERROR		(1 << 3)
#define GSM_NO_SIM		(1 << 4)
#define GSM_INIT		(1 << 5)
#define GSM_INIT_ERROR		(1 << 6)
#define GSM_NO_NETWORK		(1 << 7)
#define GSM_SEND_SMS		(1 << 8)
#define GSM_MMC_ACCESS		(1 << 9)
#define GSM_GPRS_CALL		(1 << 10)
#define GSM_GPRS_CONNECTION	(1 << 11)
#define GSM_GPRS_NOT_AVAILABLE	(1 << 12)

#define MODEM_RESET	1

// unique identifier for GSM shared memory:
#define GSMDATA_SHM_FILENAME		"/dev/scsm_gsm"
#define GSMDATA_SHM_VERSION		'A'

/*
typedef struct {
 int Signal;
 char Name[20];
 char Provider[20];
 char Version[20];
 int SimCard;
 int Status;
 int Net;
 char IMEI[20];
 char setPin[5];
 char ServiceCentre[30];
 } GSM_Struct;
*/
typedef struct {
 int Signal;
 char Name[20];
 char Provider[20];
 char Version[20];
 int SimCard;
 int Status;
 int Net;
 char IMEI[20];
 char setPin[5];
 int  NewPin;
 int PinRequired;
 char ServiceCentre[30];
 char SMSMessage[256];
 char SMSCalledNumber[64];
 //int  SMSMessageRef;
 int  SMSResult;
 int  SMSResultRef;
 //MMC communication
 int MMC_Request;
 int MMC_Status;
 char SIM_ID[30];
} GSM_Struct;

GSM_Struct /* GSM_Data={0},*/ *Shared_GSM_Data;
void *shared_memory = (void*)0;
int shmid;
int 	wait_time;
int fhandle;


time_t StartTime;

int ReInitModem();

void Exit(int ex)
 {//lprintf( L_MESG, "Exit(%d)",ex);
  mdm_release_tty();
  if(shmdt(shared_memory) == -1)
   {lprintf( L_MESG, "shmdt failed! Speicher konnte nicht freigegeben werden!");
    exit(ex);
   }

  if(shmctl(shmid, IPC_RMID, 0) == -1)
   lprintf( L_MESG, "Speicher konnte nicht freigegeben werden!");
  else
   lprintf( L_MESG, "Speicher freigegeben !");
  exit(ex);
 }


/* how much time may pass between two RINGs until mgetty goes into */
/* "waiting" state again */
int     ring_chat_timeout = 10;

/* what kind of "surprising" things are recognized */
chat_action_t	ring_chat_actions[] = { { "CONNECT",	A_CONN },
					{ "NO CARRIER", A_FAIL },
					{ "BUSY",	A_FAIL },
					{ "ERROR",	A_FAIL },
					{ "+FCON",	A_FAX  },
					{ "+FCO\r",	A_FAX  },
					{ "FAX",	A_FAX  },
					{ "+FHS:",	A_FAIL },
					{ "+FHNG:",	A_FAIL },
#ifdef VOICE
					{ "VCON",       A_VCON },
#endif
					{ NULL,		A_FAIL } };

/* the same actions are recognized while answering as are */
/* when waiting for RING, except for "CONNECT" */

chat_action_t	* answer_chat_actions = &ring_chat_actions[1];


/* prototypes for system functions (that are missing in some
 * system header files)
 */
#if !defined(__NetBSD__) && !defined(__OpenBSD__)
time_t		time _PROTO(( time_t * tloc ));
#endif

/* logname.c */
int getlogname _PROTO(( char * prompt, TIO * termio,
		char * buf, int maxsize, int max_login_time,
		boolean do_fido ));

/* conf_mg.c */
void exit_usage _PROTO((int num));

char	* Device;			/* device to use */
char	* DevID;			/* device name withouth '/'s */

extern time_t	call_start;		/* time when we sent ATA */
					/* defined in faxrec.c */

void gettermio _PROTO((char * tag, boolean first, TIO * tio));

/* "simulated RING" handler */
boolean virtual_ring = FALSE;
static RETSIGTYPE sig_pick_phone(SIG_HDLR_ARGS)
{
    signal( SIGUSR1, sig_pick_phone );
    virtual_ring = TRUE;
}
/* handle other signals: log them, and say goodbye... */
static RETSIGTYPE sig_goodbye _P1 ( (signo), int signo )
{
    lprintf( L_AUDIT, "failed dev=%s, pid=%d, got signal %d, exiting",
	              Device, getpid(), signo );
    rmlocks();
    Exit(10);
    exit(10);


}

/* create a file with the process ID of the mgetty currently
 * active on a given device in it.
 */
static char pid_file_name[ MAXPATH ];
static void make_pid_file _P0( void )
{
    FILE * fp;

    sprintf( pid_file_name, "%s/mgetty.pid.%s", VARRUNDIR, DevID );

    fp = fopen( pid_file_name, "w" );
    if ( fp == NULL )
	lprintf( L_ERROR, "can't create pid file %s", pid_file_name );
    else
    {
	fprintf( fp, "%d\n", (int) getpid() ); fclose( fp );
    }
    if ( chmod( pid_file_name, 0644 ) != 0 )
        lprintf( L_ERROR, "can't chmod() pid file" );
}


enum mgetty_States
     { St_unknown,
       St_go_to_jail,			/* reset after unwanted call */
       St_waiting,			/* wait for activity on tty */
       St_check_modem,			/* check if modem is alive */
       St_wait_for_RINGs,		/* wait for <n> RINGs before ATA */
       St_answer_phone,			/* ATA, wait for CONNECT/+FCO(N) */
       St_nologin,			/* no login allowed, wait for
					   RINGing to stop */
       St_dialout,			/* parallel dialout, wait for
					   lockfile to disappear */
       St_get_login,			/* prompt "login:", call login() */
       St_callback_login,		/* ditto, but after callback */
       St_incoming_fax,			/* +FCON detected */
       St_get_at,
       St_GSM_data,
       St_GSM_Signal,
       St_GSM_Release_tty,
       St_GSM_Request_tty,
       St_GSM_Wrong_PIN_Request_tty,
       St_GSM_Wrong_PIN_Release_tty,
       St_GSM_NETSEARCH,
       St_SMS_Send,
       St_GSM_Wrong_PIN,
       St_MMC_Card_active,
       ST_GPRS_Connection,
       St_GSM_CheckSimStatus,
   } mgetty_state = St_unknown;

/* called on SIGUSR2. Exit, if no user online, ignore otherwise */
static RETSIGTYPE sig_new_config(SIG_HDLR_ARGS)
{
    signal( SIGUSR2, sig_new_config );
    if ( mgetty_state != St_answer_phone &&
	 mgetty_state != St_get_login &&
	 mgetty_state != St_callback_login &&
	 mgetty_state != St_incoming_fax )
    {
	lprintf( L_AUDIT, "exit dev=%s, pid=%d, got signal USR2, exiting",
	              Device, getpid() );
	rmlocks();
	Exit(0);
	exit(0);

    }
    lprintf( L_MESG, "Got SIGUSR2, modem is off-hook --> ignored" );
}

enum mgetty_States st_sig_callback _P2( (pid, devname),
				        int pid, char * devname )
{
    TIO tio;

    lprintf( L_MESG, "Got callback signal from pid=%d!", pid );

    /* reopen device */
    if ( mg_open_device( devname, FALSE ) == ERROR )
    {
	lprintf( L_FATAL, "stsc: can't reopen device" );
	Exit(0);
	exit(0);
     }

    /* setup device (but do *NOT*!! set speed) */
    if ( tio_get( STDIN, &tio ) == ERROR )
    {
	lprintf( L_FATAL, "stsc: can't get TIO" ); Exit(0);exit(0);
    }
    tio_mode_sane( &tio, c_bool( ignore_carrier ) );
    tio_default_cc( &tio );
    tio_mode_raw( &tio );
    tio_set_flow_control( STDIN, &tio, DATA_FLOW );
    if ( tio_set( STDIN, &tio ) == ERROR )
    {
	lprintf( L_FATAL, "stsc: can't set TIO" ); /*exit(0);*/Exit(0);exit(0);
    }

    /* make line controlling tty */
    mg_get_ctty( STDIN, devname );

    /* steal lock file from callback process */
    lprintf( L_MESG, "stealing lock file from pid=%d", pid );
    if ( steal_lock( Device, pid ) == ERROR ) return St_dialout;

    /* signal user */
    printf( "...ok\r\n" );

    /* signal callback process (but give it some time to enter pause()! */
    delay(500);
    if ( kill( pid, SIGUSR1 ) < 0 )
    {
	lprintf( L_ERROR, "can't signal callback process" );
    }

    /* now give user a login prompt! */
    return St_callback_login;
}

/* line locked, parallel dialout in process.
 *
 * Two things can happen now:
 *   - lock file disappears --> dialout terminated, exit(), restart
 *   - get signal SIGUSR1 --> dialout was callback, mgetty takes over
 */
enum mgetty_States st_dialout _P1( (devname), char * devname )
{
    int pid;

    /* the line is locked, a parallel dialout is in process */

    virtual_ring = FALSE;			/* used to signal callback */

    /* write a note to utmp/wtmp about dialout, including process args
     * (don't do this on two-user-license systems!)
     */
#ifndef USER_LIMIT
    pid = checklock( Device );		/* !! FIXME, ugly */
    make_utmp_wtmp( Device, UT_USER, "dialout", get_ps_args(pid) );
#endif

    /* close all file descriptors -> other processes can read port */
    close(0);
    close(1);
    close(2);

    /* this is kind of tricky: sometimes uucico dial-outs do still
       collide with mgetty. So, when my uucico times out, I do
       *immediately* restart it. The double check makes sure that
       mgetty gives me at least 5 seconds to restart uucico */
    lprintf( L_MESG, "st_dialout waiting for completion!");
    do {
	/* wait for lock to disappear */
	while ( ( pid = checklock(Device) ) != NO_LOCK )
	{
	    sleep(10);

	    /* virtual ring? this would mean an active callback! */
	    if ( virtual_ring )
	    {
		return st_sig_callback( pid, devname );
	    }
	}

	/* wait a moment, then check for reappearing locks */
	sleep(5);
    }
    while ( checklock(Device) != NO_LOCK );

    /* OK, leave & get restarted by init */
    lprintf( L_MESG, "st_dialout Exit(0)");
    Exit(0);
    exit(0);
}					/* end st_dialout() */

void get_ugid _PROTO(( conf_data * user, conf_data * group,
			uid_t * uid, gid_t * gid ));


void GSM_State(int State)
 {wait_time = 30000;

  if( Shared_GSM_Data->Status == GSM_NO_SIM)    return;
  if( Shared_GSM_Data->Status == GSM_PIN_ERROR)
   {if(State != GSM_INIT)
     return;
   }

   switch(State)
    {case GSM_READY:	Shared_GSM_Data->Status=GSM_READY;
			lprintf( L_MESG, "GSM STATE = GSM_READY (%d)",Shared_GSM_Data->Status);
			break;
     case GSM_CALL:	Shared_GSM_Data->Status=GSM_CALL;
			lprintf( L_MESG, "GSM STATE = GSM_CALL (%d)",Shared_GSM_Data->Status);
			break;
     case GSM_CONNECTION:Shared_GSM_Data->Status=GSM_CONNECTION;
			lprintf( L_MESG, "GSM STATE = GSM_CONNECTION (%d)",Shared_GSM_Data->Status);
			//scLogEventText(SCLOGEVT_GSM_DIALEDIN, "\0");
			break;
     case GSM_NO_SIM:	Shared_GSM_Data->Status=GSM_NO_SIM;
			lprintf( L_MESG, "GSM STATE = GSM_NO_SIM (%d)",Shared_GSM_Data->Status);
			break;
     case GSM_PIN_ERROR:Shared_GSM_Data->Status=GSM_PIN_ERROR;
			lprintf( L_MESG, "GSM STATE = GSM_PIN_ERROR (%d)",Shared_GSM_Data->Status);
			break;
     case GSM_INIT:	Shared_GSM_Data->Status=GSM_INIT;
			lprintf( L_MESG, "GSM STATE = GSM_INIT (%d)",Shared_GSM_Data->Status);
			break;
     case GSM_GPRS_CALL:Shared_GSM_Data->Status=GSM_GPRS_CALL;
			lprintf( L_MESG, "GSM STATE = GSM_GPRS_CALL (%d)",Shared_GSM_Data->Status);
			break;
     case GSM_NO_NETWORK:Shared_GSM_Data->Status=GSM_NO_NETWORK;
			wait_time = 5000;
			lprintf( L_MESG, "GSM STATE = GSM_NO_NETWORK (%d)",Shared_GSM_Data->Status);
			break;
     case GSM_GPRS_NOT_AVAILABLE:
			Shared_GSM_Data->Status=GSM_GPRS_NOT_AVAILABLE;
			lprintf( L_MESG, "GSM STATE = GSM_GPRS_NOT_AVAILABLE (%d)",Shared_GSM_Data->Status);
			break;

    }
 }


int CheckPin(char *Pin)
 {char valid[12]={"0123456789"};

  lprintf( L_MESG, "CheckPin(%s) strlen(Pin)=%d",Pin,strlen(Pin));
  if( strlen(Pin) == 0)
   {lprintf( L_MESG, "CheckPin(%s) -> Fehler!",Pin);
    return(0);
   }
  if(strspn(Pin,valid) != strlen(Pin))
   {lprintf( L_MESG, "CheckPin(%s) -> Fehler!",Pin);
    return(0);
   }
  lprintf( L_MESG, "CheckPin(%s) -> OK",Pin);
  return(1);
 }

int TEMP(char *Config[], char *pin)
 {if(strlen(pin) <= 4)
   {sprintf(Config[1],"AT+CPIN=%s",pin);
    return(1);
   }
  return(0);
 }

int GetConfigPin(char *Config[], char *pin)
 {char Pin_temp[20];
  strncpy(Pin_temp,Config[1],19);
  sscanf(Pin_temp,"AT+CPIN=%s",pin);


  if(strlen(pin) > 4)
   {strcpy(pin,"\0");
    return(-1);
   }

  if(!CheckPin(pin))
   return(-1);

  //return 0  -> keine Pin
  //return 1  -> Pin OK
  //return -1 -> Fehler
  return(1);
 }

int SMS_Limit()
 {return(3);
 }


#define SMS_MESSAGE_SIZE 250
int Send_SMS(char *Number , char *Message , int Email)
 {return(mdm_Send_SMS (Number,Message,STDIN));
 }

int SetSpeed(int Speed)
 {char a[50]="AT+CSQ",aa[50],*pa;
  pa=&aa[0];

  strcpy(a,"AT+CSNS=4");*pa=0;
  pa = mdm_get_idstring(&a[0], 1, STDIN );

  strcpy(a,"AT+CBST=14");*pa=0;
  pa = mdm_get_idstring(&a[0], 1, STDIN );

  //lprintf( L_MESG, "### Set Speed -> %s",pa);

  return(1);
 }


int ModemSoftReset()
{char a[50]="AT+CFUN=1,1";
 int ret;

 lprintf( L_WARN, "!!!! Modem soft reset !!!");
 ret = mdm_command(&a[0], STDIN );
 lprintf( L_MESG, "!!!! Modem soft reset -> Waiting 5 seconds ... !!!");
 sleep(5); 		// See datasheet

 return(ret);
}

int GetSimState(int *SimInserted, int *PinState)
{char a[50]="",aa[50],*pa;
 pa=&aa[0];
 int retry = 0;
 int ret = 1;

 *SimInserted = -1;	//Unknown
 *PinState = -1;	//Unknown

 lprintf( L_MESG, "GetSimState()");

 strcpy(a,"AT^SCKS?");*pa=0;

 ret = mdm_get_string(&a[0], 1, STDIN, &pa );
 lprintf( L_MESG, "GetSimState() - AT^SCKS? -> %s (ret=%d -> %s)",pa,ret,(ret==1)?"OK":"ERROR");

 if(ret != 1)
  {return(0); //Error AT command
  }

 if( (*pa == '^') && (*(pa+1) == 'S') && (*(pa+2) == 'C') && (*(pa+3) == 'K') && (*(pa+4) == 'S') && (*(pa+5) == ':') && (*(pa+6) == ' ') && (*(pa+8) == ',') && (*(pa+9) == '1'))
  {//SIM card is inserted

   *SimInserted = 1;
   lprintf( L_MESG, "GetSimState() -> SIM card found! (SimInserted=%d)",*SimInserted);

   while(retry < 3)
    {
     strcpy(a,"AT+CPIN?");*pa=0;
     ret = mdm_get_string(&a[0], 1, STDIN, &pa );
     lprintf( L_MESG, "GetSimState() - AT+CPIN? -> %s (ret=%d -> %s)",pa,ret,(ret==1)?"OK":"ERROR");

     if(ret != 1)
      {//AT command: Error
       ret = 0;
      }
     else
      {//AT command: OK
       ret = 1;

       if(strstr(pa,"+CPIN: READY") != NULL)
        {*PinState = 0;		//No PIN neccessary
	 break;
        }

       if(strstr(pa,"+CPIN: SIM PIN") != NULL)
        {*PinState = 1;		//PIN neccessary
         break;
        }
      }

     retry++;
     sleep(2);
     lprintf( L_MESG, "GetSimState() -> Retry %d !",retry);
    }
  }
 else
  {//No SIM inserted
   lprintf( L_MESG, "GetSimState() -> No SIM card!");
   ret = 1;
   *SimInserted  = 0;
   *PinState     = -1;
  }

 return(ret);
}

int SimStateCheck()
{int Reset = 1,Ret;
 int SimInserted, PinState;
 time_t Time;

#define RESET	60 * 60 * 3	//3 hours

 lprintf( L_MESG, "SimStateCheck()");

 Ret = GetSimState(&SimInserted, &PinState);
 lprintf( L_MESG, "SimStateCheck() -> GetSimState(SimInserted=%d, PinState=%d) ret=%d",SimInserted,PinState,Ret);

 if( Ret == 0)
  {lprintf( L_MESG, "SimStateCheck() -> Error! -> Reset",Ret,SimInserted,PinState);
   Shared_GSM_Data->Status = GSM_NO_SIM;
   ModemSoftReset();
   lprintf( L_MESG, "SimStateCheck() -> Exit process");
   Exit(0);
  }

 if(SimInserted == 1)
  {
   switch(PinState)
    {
	case 0:	//Ready
			if((Shared_GSM_Data->Status == GSM_NO_SIM) || (Shared_GSM_Data->Status == GSM_PIN_ERROR))
        		 Shared_GSM_Data->Status = GSM_NO_NETWORK;
			Reset = 0;
			lprintf( L_MESG, "SimStateCheck() -> SIM card inserted, PinState = %d -> READY",PinState);
			break;
	case 1:	//SIM PIN
			Shared_GSM_Data->Status = GSM_NO_SIM;
			Reset = 1;
			lprintf( L_MESG, "SimStateCheck() -> SIM card inserted, PinState = %d -> SIM PIN",PinState);
			break;
	default:	//Unknown
			Shared_GSM_Data->Status = GSM_PIN_ERROR;
			Reset = 0;
			lprintf( L_MESG, "SimStateCheck() -> SIM card inserted, PinState = %d -> (Not supported or unknown)",PinState);
			break;
    }
  }
 else
  {Shared_GSM_Data->Status = GSM_NO_SIM;
   time(&Time);

   if(Time > StartTime)
    {if( (Time - StartTime) > (RESET) ) // 3 hours
      {Reset = 0;
       StartTime = Time;
       ReInitModem();
      }
     else
      Reset = 0;
     lprintf( L_MESG, "SimStateCheck() -> No SIM card! (Next reset in %ld seconds) -> Reset=%d",RESET - (Time - StartTime),Reset);
    }
   else
    {Reset = 1;
     lprintf( L_MESG, "SimStateCheck() -> No SIM card! -> Reset=%d",Reset);
    }
  }

 if(Reset == 1)
   {lprintf( L_MESG, "SimStateCheck() -> SimInserted=%d, PinState=%d -> Reset!",SimInserted,PinState);
    Shared_GSM_Data->Status = GSM_NO_SIM;
    ModemSoftReset();
    lprintf( L_MESG, "SimStateCheck() -> Exit process");
    Exit(0);
   }
 else
  lprintf( L_MESG, "SimStateCheck() -> OK");

 return(Reset);
}

int GetPinCounter()
{char a[50]="AT^SPIC",aa[50],*pa;
 int Ret,Pin_Cnt=-1;
 pa=&aa[0];

 Ret = mdm_get_string(&a[0], 1, STDIN, &pa );

 if(Ret != 1)
  {lprintf( L_MESG, "GetPinCounter() -> Error (Return -1)");
   return -1; //Error
  }

 if((*(pa+0) == '^') && (*(pa+1) == 'S') && (*(pa+2) == 'P') && (*(pa+3) == 'I') && (*(pa+4) == 'C') && (*(pa+5) == ':') && (*(pa+6) == ' '))
  {sscanf(pa,"^SPIC: %d",&Pin_Cnt);
  }

 lprintf( L_MESG, "GetPinCounter() -> Return %d)",Pin_Cnt);

 return (Pin_Cnt);
}

int EnterPin(char *pin)
 {char aa[50],*pa;
  pa=&aa[0];
  int Pin_Cnt=0;

  char ConfigPin[10];
  int PinStatus=0;
  int Ret, SimInserted, PinState;

  if(pin == NULL)
   GetConfigPin(c_chat(pin_chat),ConfigPin);
  else
  if(Shared_GSM_Data->NewPin == 1)
   {TEMP(c_chat(pin_chat),Shared_GSM_Data->setPin);
    GetConfigPin(c_chat(pin_chat),ConfigPin);
    lprintf( L_MESG, "*** New pin=%s",ConfigPin);
    Shared_GSM_Data->NewPin=0;
   }

  Shared_GSM_Data->SimCard=0;	//No SIM Card inserted

  Ret = GetSimState(&SimInserted, &PinState);
  lprintf( L_MESG, "EnterPin() -> GetSimState(SimInserted=%d, PinState=%d) returned %d (%s)!",SimInserted,PinState,Ret,(Ret==1)?"OK":"Error");


  if(Ret == 0)
   {lprintf( L_MESG, "EnterPin() - Error -> Reset!");
    Shared_GSM_Data->Status = GSM_NO_SIM;
    ModemSoftReset();
    lprintf( L_MESG, "EnterPin() -> Exit process");
    Exit(0);
   }


  if(SimInserted == 1)
   {Shared_GSM_Data->SimCard=1;
    lprintf( L_MESG, "EnterPin() -> SIM inserted! (%d)",Shared_GSM_Data->Status);

    switch(PinState)
     {
	case 0:	//READY
			lprintf( L_MESG, "EnterPin() -> READY!");
			Shared_GSM_Data->PinRequired=0;
			PinStatus=0;
			break;

	case 1:	//SIM PIN
			lprintf( L_MESG, "EnterPin() -> SIM PIN!");
        		PinStatus=1;
			Shared_GSM_Data->PinRequired=1;
			break;

	default:	//Not supported or unknown
			lprintf( L_MESG, "EnterPin() -> Unknown or not supported!");
			Shared_GSM_Data->PinRequired=1;
        		GSM_State(GSM_PIN_ERROR);
			PinStatus=0;
			break;

     }

   }
  else
   {GSM_State(GSM_NO_SIM);
    lprintf( L_MESG, "EnterPin() -> No SIM!(%d)",Shared_GSM_Data->Status);
    PinStatus=0;
   }


  if(PinStatus == 1)
   {//PIN needed

    sleep(1);

     //Pin Counter
    if( (Pin_Cnt = GetPinCounter()) < 0)
     {lprintf( L_MESG, "EnterPin() -> GetPinCounter() Error (Ret = %d)! -> Reset",Pin_Cnt);
      Shared_GSM_Data->Status = GSM_PIN_ERROR;
      sleep(60);
      ModemSoftReset();
      lprintf( L_MESG, "EnterPin() -> Exit process");
      Exit(0);
     }

    if( (Pin_Cnt >= ((pin==NULL)?3:1)) && (CheckPin(ConfigPin)))
     {//Write PIN
      mdm_lock_tty();
      if ( mg_init_data( STDIN, c_chat(pin_chat), c_bool(need_dsr), NULL ) == FAIL )
       {mdm_release_tty();
        lprintf( L_AUDIT, "EnterPin() -> Error enter PIN dev=%s, pid=%d", Device, getpid() );
        GSM_State(GSM_PIN_ERROR);
       }
      else
       {mdm_release_tty();
        lprintf( L_MESG, "EnterPin() -> OK!");
       }
     }
    else
     {GSM_State(GSM_PIN_ERROR);
      strcpy(Shared_GSM_Data->setPin,"\0");
     }
   }

  return(1);

}

void SendAT()
 {char *pa,a[10]="AT",aa[50]="";
  pa=&aa[0];
  int ret=0,i;

  for(i=0 ; i < 3 ; i++)
   {lprintf(L_MESG, "*** SendAT() - Sending AT for Autobauding %d",i+1);
    ret = mdm_command (&a[0], STDIN );

    if(ret == NOERROR)
     {lprintf( L_MESG, "*** SendAT() - OK",pa);
      clean_line( STDIN, 1);
      break;
     }
    else
     {lprintf( L_MESG, "*** SendAT() - ERROR",pa);
      clean_line( STDIN, 1);
     }
   }

  //lprintf(L_MESG, "*** SendAT() - (1) Sending AT for Autobauding");
  //pa = mdm_get_idstring(&a[0], 1, STDIN );
  //lprintf(L_MESG, "*** SendAT() - pa = %s",pa);

  //clean_line( STDIN, 1);
 }


int ReInitModem()
{ char a[50]="AT+CFUN=1,1";
  int ret;

  lprintf( L_MESG, "ReInitModem() -> Reset Modem");

  ret = mdm_command(&a[0], STDIN );

  sleep(6);

  SendAT(); //For Autobauding

  mdm_lock_tty();

  if ( mg_init_data( STDIN, c_chat(init_chat), c_bool(need_dsr), c_chat(force_init_chat) ) == FAIL )
	{
	    mdm_release_tty();
	    lprintf( L_AUDIT, "ReInitModem() -> failed in mg_init_data, dev=%s, pid=%d", Device, getpid() );

	    sleep(2);

	    mdm_lock_tty();
	    if ( mg_init_data( STDIN, c_chat(force_init_chat), c_bool(need_dsr),NULL ) == FAIL )
		{   mdm_release_tty();
		    tio_flush_queue( STDIN, TIO_Q_BOTH );	/* unblock flow ctrl */
		    rmlocks();
		    lprintf( L_MESG, "ReInitModem() -> Initialisierung fehlgeschlagen!");
		    Exit(1);
		}
	    mdm_release_tty();
	}
	else
	 {mdm_release_tty();
	 }

  SendAT(); //For Autobauding

  EnterPin(NULL);

  return(1);

}

/*
void CheckForNewPin()
 {char a[50]="AT+CPIN=",aa[50],*pa;
  pa=&aa[0];

  if(CheckPin(Shared_GSM_Data->setPin))
   {strcat(a,Shared_GSM_Data->setPin);
     pa = mdm_get_idstring(&a[0], 1, STDIN );
     lprintf( L_MESG, "Antwort neue Pin: %s",pa);
   }
 }
*/

#ifdef MODEM_RESET

#define FILE_MODEM_RESET	"/tmp/mgetty_ModemResetCnt"

int IncrementModemResetCnt()
 {int ResetCnt=0;
  FILE *in;
  char Line[100+1];
  int index=0,c,i;

  //TODO: Read reset counter
  in = fopen(FILE_MODEM_RESET,"a+");

  if(!in)
   return(0);

  while((c = fgetc(in)) != EOF)
   {if( c==0x0a )
     {//Line[index] = '\0';
      break;
     }
    else
     Line[index] = (char)c;
    index++;
   }
  Line[index] = '\0';

  fclose(in);

  if(sscanf(Line,"%d",&ResetCnt) == EOF)
   ResetCnt = 0;

  if( (ResetCnt < 0) || (ResetCnt >= 10000))
   ResetCnt = 4;
  else
   ResetCnt += 1;

  //Write counter value

  in = fopen(FILE_MODEM_RESET,"w+");

  if(!in)
   return (0);

  if(ResetCnt == 1) //First Reset -> Logger entry
   {// Logger entry -> "MODEM RESET"
   //scLogEventText(SCLOGEVT_MODEM_RESET, "\0");
   bdmSetSC1000Alert( BDM_ALERT_DEVWARN, 0, 1 );
   }
  else if(ResetCnt == 3)
   {// Logger entry -> "MODEM FAILURE"
    //scLogEventText(SCLOGEVT_MODEM_FAILURE, "\0");
    bdmSetSC1000Alert( BDM_ALERT_DEVERR, 5, 1 );
   }

  //TODO: Save reset counter
  sprintf(Line,"%d\x0a",ResetCnt);

  for(i=0 ; i<strlen(Line) ; i++)
   {c = (int)*(Line+i);
    putc(c,in);
   }
  fclose(in);

  if((ResetCnt > 3) && (ResetCnt <= 10))
   {lprintf( L_WARN, "Modem failure! -> waiting 1 minute (ResetCnt=%d)",ResetCnt);
    sleep(60);
   }
  if(ResetCnt > 10)
   {lprintf( L_WARN, "Modem failure! -> waiting 10 minutes (ResetCnt=%d)",ResetCnt);
    sleep(600);
   }
  else
   {lprintf( L_WARN, "Modem failure! -> waiting 10 seconds (ResetCnt=%d)",ResetCnt);
    sleep(10);
   }

  return(1);
 }

void ClearModemResetCnt()
 {char System[100];

  sprintf(System,"rm -f %s",FILE_MODEM_RESET);

  //Clear Errror an Warning flags
  bdmSetSC1000Alert( BDM_ALERT_DEVWARN, 0, 0 );
  bdmSetSC1000Alert( BDM_ALERT_DEVERR, 5, 0 );

  system(System);
 }

int SwitchOffModule()
 {lprintf( L_MESG, "Modem power off!" );

  //Power off
#ifdef __SH3__
  ioctl(STDIN, TIO_SET_MODEMPWR, 0);
#endif
  IncrementModemResetCnt();

  return(1);
 }
#endif

#ifdef TIO_SET_MMC
int SwitchOnModule()
 {int gsm_is_plugged;

  lprintf( L_MESG, "Suche GSM-Modem!" );

  ioctl(STDIN, TIO_CHK_GSM, &gsm_is_plugged);			// pin state holen

  if(gsm_is_plugged)
   { lprintf( L_MESG, "GSM-Modem ist gesteckt! -> Switching on!" );

     mdm_lock_tty();

     //Power on (IO Controll)
     ioctl(STDIN, TIO_SET_MODEMPWR, 1);
     sleep(1);
     ioctl(STDIN, TIO_SET_IGT, 1);
     sleep(1); ioctl(STDIN, TIO_SET_IGT, 0);
     sleep(1); ioctl(STDIN, TIO_SET_IGT, 1);
     sleep(2);
     Shared_GSM_Data->MMC_Status=0;

      mdm_release_tty();
   }
  else
   {lprintf( L_MESG, "GSM-Modem ist nicht gesteckt! -> releaseMMCModem( RESOURCE_INDEX_MODEM )" );
    releaseMMCModem( RESOURCE_INDEX_MODEM );
    Shared_GSM_Data->MMC_Status=1;
    mdm_release_tty();
    while(1){sleep(100);}
   }
  return(1);
  }

#endif





int CheckLogFileSize(char *Log_path , char *Device , unsigned int size)
 {FILE *indata;
  struct stat statbuf;
  char command[50];
  //unsigned int filesize=0;
  char LogFormat[100];
  char LogFile[110];



  sprintf(LogFormat,"%s",Log_path);
  sprintf(LogFile, LogFormat , Device);
  //lprintf(  L_MESG, "Log File: %s ", LogFile );

  indata=fopen(LogFile, "r");
   if (!indata)
    {//lprintf(L_MESG , "LogClient: Cannot open logfile %s",LogFile);
     return (-1);
    }
   else
    {fstat(indata->_fileno, &statbuf);
     //lprintf(L_MESG , "File : %s Filesize %d byte",LogFile ,(unsigned int)statbuf.st_size);
     //filesize=statbuf.st_size;
     if((unsigned int)statbuf.st_size > size)
      {sprintf(command , "rm %s" , LogFile);
       system(command);
       fclose(indata);
       return((int)statbuf.st_size);
      }
    }
  fclose(indata);
  return(0);
 }


int OpenCom(uid_t uid ,gid_t gid,char * devname, int FirstInit , int speed)
 {char buf[MAXLINE+1];
  int i=0;
  int Speed;
  lprintf(L_MESG, "*** OpenCom() fhandle=%d",fhandle);


   /* the line is mine now ...  */

    /* set proper port ownership and permissions
     */
    get_ugid( &c.port_owner, &c.port_group, &uid, &gid );
    chown( devname, uid, gid );
    if ( c_isset(port_mode) )
	chmod( devname, c_int(port_mode) );

    /* if necessary, kill any processes that still has the serial device
     * open (Marc Boucher, Marc Schaefer).
     */
#if defined( EXEC_FUSER )
    lprintf( L_MESG, "Kill processes use ttySC0");
    sprintf( buf, EXEC_FUSER, devname );
    if ( ( i = system( buf ) ) != 0 )
        lprintf( L_MESG, "%s: return code %d", buf, i );
#endif

    /* setup terminal */

    /* Currently, the tio set here is ignored.
       The invocation is only for the sideeffects of:
       - loading the gettydefs file if enabled.
       - setting port speed appropriately, if not set yet.
       */
    gettermio(c_string(gettydefs_tag), TRUE, (TIO *) NULL);

    if(speed == 0)
     Speed = c_int(speed);
    else
     Speed = speed;

    /* open + initialize device (mg_m_init.c) */
    if ( mg_get_device( devname, c_bool(blocking),
		        c_bool(toggle_dtr), c_int(toggle_dtr_waittime),
		        Speed/*c_int(speed)*/ ) == ERROR )
    {
	lprintf( L_FATAL, "cannot get terminal line dev=%s, exiting", Device);
	//exit(30);
        Exit(30);
	exit(30);
    }

  clean_line( STDIN, 1);

  if(FirstInit==0)
   SendAT();

  lprintf(L_MESG, "*** OpenCom() - done fhandle=%d , Speed = %d",fhandle,Speed);
  return(0);

 }

int CloseCom()
 {lprintf(L_MESG, "*** CloseCom() - (0)(1)(2)");

  close(0);
  close(1);
  close(2);

  if(fhandle >= 3)
   {lprintf(L_MESG, "*** CloseCom() - (%d)",fhandle);
    close(fhandle);
    fhandle=-1;
   }

  return(0);
 }

#define LineLength 100
int ReadValueFromFile (char *PathFile , char *Value)
 {int c;
  FILE *in = 0;
  char Line[LineLength+1];
  int LineCNT=0;
  char *pLine=Line;
  //char *pIP=0;


  in = fopen(PathFile,"rb+");

  if(!in)
   {strcpy(Value,"\0");
    return 0;
   }

 memset(Line, 0 , LineLength + 1);


  while((c = fgetc(in)) != EOF)
   {if(LineCNT < LineLength)Line[LineCNT] = (char)c;
    else
     {Line[LineLength]='\0';
     }

    if( c==0x0a )
     {if(LineCNT < LineLength) Line[LineCNT] = '\0';
      strcpy(Value , pLine);
      fclose(in);
      return(1);
     }
    else LineCNT++;

   }

  /*
  while((c = fgetc(in)) != EOF)
   {if(LineCNT < LineLength)Line[LineCNT] = (char)c;
    else
     {Line[LineLength]='\0';
     }
   LineCNT++;
   }
*/
  fclose(in);

  if(strlen(Line) > 0)
   {strcpy(Value , pLine);
    return(1);
   }

  return 0;


  }

int GPRS_enabled()
 {char Value[10];
  int Index=0;

  if(ReadValueFromFile ("/mnt/sc1000/set/gprs_connection" , Value))
   {if(sscanf(Value,"%d",&Index) != EOF)
     {switch(Index)
	{case 1:	return(1);
			break;
	 default:	return(0);
			break;
	}
     }
   }
  return(0);
 }

void CheckFailedGprsDialIn()
{FILE *in = 0;
 char Counter[20];
 int counter=0;

 in = fopen("/tmp/RequestModemReset","r");

 if(in == NULL)
  return;

 fclose(in);
 sleep(1);
 system("rm -f /tmp/RequestModemReset");
 lprintf( L_MESG, "Found failed GPRS Dial in file -> Modem SoftReset!");

 ModemSoftReset();

  //Power off
#ifdef __SH3__
  ioctl(STDIN, TIO_SET_MODEMPWR, 0);
#endif

 CloseCom();

 //Counter
 if(ReadValueFromFile ("/tmp/RequestModemResetCounter" , Counter) == 0)
  strcpy(Counter,"0");

 if(sscanf(Counter,"%d",&counter) != 1)
  counter = 0;

 if(counter >= 99999)
  counter = 0;
 else
  counter++;

 sprintf(Counter,"%d",counter);

 WriteValueToFile("/tmp/RequestModemResetCounter" , Counter);





 Exit(1);
}

int OperatorSelection()
{char MMC_MNC[20];
 static char MMC_MNC_OLD[20] = "xxx";
 char a[50];

 SC_Get_Operator(MMC_MNC);

 if( strcmp(MMC_MNC , MMC_MNC_OLD) == 0 )
  {lprintf( L_MESG, "Operator select -> Nothing to do!");
   return(0);
  }

 if((strlen(MMC_MNC) == 1) && (MMC_MNC[0] == '0')) 	// Automatic mode
  {lprintf( L_MESG, "Operator select -> Automatic");
   strcpy(a,"AT+COPS=0,0");
   mdm_command(&a[0], STDIN );
  }
 else 				// Manual Operator selection
  {lprintf( L_MESG, "Operator select -> %s",MMC_MNC);
   sprintf(a,"AT+COPS=1,2,%s",MMC_MNC);
   mdm_command(&a[0], STDIN );

   // Set Long alphanumeric formate
   strcpy(a,"AT+COPS=3,0");
   mdm_command(&a[0], STDIN );
  }

 strcpy(MMC_MNC_OLD,MMC_MNC);

 return(1);
}



int main _P2((argc, argv), int argc, char ** argv)
{
    char devname[MAXLINE+1];		/* full device name (with /dev/) */
    char buf[MAXLINE+1];
    TIO	tio;
    FILE *fp;
    int i;
    char *pchar1=NULL,*pchar2=NULL;
    action_t	what_action;
    int		rings_wanted;
    int		rings = 0;
    int		dist_ring = 0;		/* type of RING detected */
    double 	Empfang=0;
    wait_time = 30 * 1000;	//30 sec
    int GPRS_Enabled=0,GPRS_Available=0;
    char MMC_MNC[7];

    setApplicationId( APPLICATION_ID_TESTCLIENT );

#if defined(_3B1_) || defined(MEIBE) || defined(sysV68)
    extern struct passwd *getpwuid(), *getpwnam();
#endif

    fhandle=-1;

    //GSM_Struct /* GSM_Data={0},*/ *Shared_GSM_Data;
    //void *shared_memory = (void*)0;
    //int shmid;


    uid_t	uid;			/* typical uid for UUCP */
    gid_t	gid;

#ifdef VOICE
    boolean	use_voice_mode = TRUE;
#endif
    char a[50]="AT+CSQ",aa[50],*pa;
    pa=&aa[0];
    int Wert1=0,Wert2=0;


    sleep(3);
    /* startup: initialize all signal handlers *NOW*
     */
    (void) signal(SIGHUP, SIG_IGN);

    /* set to remove lockfile(s) and print "got signal..." message
     */
    (void) signal(SIGINT, sig_goodbye);
    (void) signal(SIGQUIT, sig_goodbye);
    (void) signal(SIGTERM, sig_goodbye);

    /* sometimes it may be desired to have mgetty pick up the phone even
       if it didn't RING often enough (because you accidently picked it up
       manually...) or if it didn't RING at all (because you have a fax
       machine directly attached to the modem...), so send mgetty a signal
       SIGUSR1 and it will behave as if a RING was seen
       In addition, this is used by the "callback" module.
       */
    signal( SIGUSR1, sig_pick_phone );

    /* for reloading the configuration file, we need a way to tell mgetty
       "restart, but only if no user is online". Use SIGUSR2 for that
       */
    signal( SIGUSR2, sig_new_config );

#ifdef HAVE_SIGINTERRUPT
    /* some systems, notable BSD 4.3, have to be told that system
     * calls are not to be automatically restarted after those signals.
     */
    siginterrupt( SIGINT,  TRUE );
    siginterrupt( SIGALRM, TRUE );
    siginterrupt( SIGHUP,  TRUE );
    siginterrupt( SIGUSR1, TRUE );
    siginterrupt( SIGUSR2, TRUE );
#endif

    Device = "unknown";

    /* process the command line
     */
    mgetty_parse_args( argc, argv );

    /* normal System V getty argument handling
     */

    if (optind < argc)
        Device = argv[optind++];
    else {
	lprintf(L_FATAL,"no line given");
	exit_usage(2);
    }

   i = requestMMCModem( RESOURCE_INDEX_MODEM, 0 );
   //lprintf( L_MESG, "GSM-Modem ist nicht gesteckt! -> requestMMCModem( RESOURCE_INDEX_MODEM, 0 ) = %d",i );
   if( i <= 0)
     {//lprintf(L_FATAL,"main() -> Serial line busy! ret=%d",i);
      sleep(1);
      Exit(0);
     }


    /* remove leading /dev/ prefix */
    if ( strncmp( Device, "/dev/", 5 ) == 0 ) Device += 5;

    /* need full name of the device */
    sprintf( devname, "/dev/%s", Device);

    /* Device ID = Device name without "/dev/", all '/' converted to '-' */
    DevID = mydup( Device );
    for ( i=0; DevID[i] != 0; i++ )
        if ( DevID[i] == '/' ) DevID[i] = '-';

    /*Check log file size and remove if > size */
#define MAX_LOGFILE_SIZE	5000
    int LogFileSize=0;
    LogFileSize = CheckLogFileSize(LOG_PATH , Device , MAX_LOGFILE_SIZE);

    /* name of the logfile is device-dependant */
    sprintf( buf, LOG_PATH, DevID );
    log_init_paths( argv[0], buf, &Device[strlen(Device)-3] );

    if(LogFileSize > 0)
     lprintf(  L_MESG, "LogFileSize > %d -> File removed!", MAX_LOGFILE_SIZE );

    //*********** shared memory ****************************************
    key_t key = ftok( GSMDATA_SHM_FILENAME, GSMDATA_SHM_VERSION );

    shmid = shmget(/*(key_t)2345*/key, sizeof(GSM_Struct), 0666 | IPC_CREAT);
    if(shmid == -1)
     {
     lprintf(  L_MESG, "shmget failed!" );
     Exit(1);
     }

     shared_memory = shmat(shmid, (void*)0, 0);

    if( shared_memory == (void*) -1)
     {lprintf(  L_MESG, "shmat failed!" );
      Exit(1);
     }
    else
     {lprintf( L_MESG, "Shard Memory attached at %X, Key: %X", (int) shared_memory,(int)key );
     }

    Shared_GSM_Data = (GSM_Struct* )shared_memory ;
    memset(Shared_GSM_Data, 0 , sizeof(GSM_Struct));
    GSM_State(GSM_INIT);
    Shared_GSM_Data->SMSResult=1;
    Shared_GSM_Data->SMSResultRef=0;
    Shared_GSM_Data->NewPin=0;
    strcpy(Shared_GSM_Data->Provider,"?");
    Shared_GSM_Data->PinRequired=1;
    Shared_GSM_Data->MMC_Request=0;
    Shared_GSM_Data->MMC_Status=0;
    //********************************************************************


#ifdef VOICE
    lprintf( L_MESG, "vgetty: %s", vgetty_version);
#endif
    lprintf( L_MESG, "mgetty: %s", mgetty_version);
    lprintf( L_NOISE, "%s compiled at %s, %s", __FILE__, __DATE__, __TIME__ );
    lprintf( L_NOISE, "user id: %d, pid: %d, parent pid: %d", getuid(), getpid(), getppid());

    /* read configuration file */
    mgetty_get_config( Device );

#ifdef VOICE
    check_system();
    voice_config("vgetty", DevID);
    voice_register_event_handler(vgetty_handle_event);
#endif

#ifdef USE_GETTYDEFS
    if (optind < argc)
        conf_set_string( &c.gettydefs_tag, argv[optind++] );

    lprintf( L_MESG, "gettydefs tag used: %s", c_string(gettydefs_tag) );
#endif

    make_pid_file();

    lprintf(L_MESG, "check for lockfiles");

    /* deal with the lockfiles; we don't want to charge
     * ahead if uucp, kermit or whatever else is already
     * using the line.
     * (Well... if we reach this point, most propably init has
     * hung up anyway :-( )
     */

    /* check for existing lock file(s)
     */
    if (checklock(Device) != NO_LOCK)
    {
	st_dialout(NULL);
    }


    /* try to lock the line
     */
    lprintf(L_MESG, "locking the line");

    if ( makelock(Device) == FAIL )
    {
	st_dialout(NULL);
    }


//********************** Versuch, Verklemmungen zu loesen! *****************
#ifdef TIO_SET_MMC
    tio_Reset( STDIN );
#endif
//**************************************************************************
OpenCom(uid,gid,devname,1,0);
/*
//--------------------- OpenCom --------------------------------------------
    // the line is mine now ...

    // set proper port ownership and permissions

    get_ugid( &c.port_owner, &c.port_group, &uid, &gid );
    chown( devname, uid, gid );
    if ( c_isset(port_mode) )
	chmod( devname, c_int(port_mode) );

    // if necessary, kill any processes that still has the serial device
    // open (Marc Boucher, Marc Schaefer).
    //
#if defined( EXEC_FUSER )
    lprintf( L_WARN, "Kill processes use ttySC0");
    sprintf( buf, EXEC_FUSER, devname );
    if ( ( i = system( buf ) ) != 0 )
        lprintf( L_WARN, "%s: return code %d", buf, i );
#endif

    // setup terminal

    // Currently, the tio set here is ignored.
    //   The invocation is only for the sideeffects of:
    //   - loading the gettydefs file if enabled.
    //   - setting port speed appropriately, if not set yet.
    //

    gettermio(c_string(gettydefs_tag), TRUE, (TIO *) NULL);
    // open + initialize device (mg_m_init.c)
    if ( mg_get_device( devname, c_bool(blocking),
		        c_bool(toggle_dtr), c_int(toggle_dtr_waittime),
		        c_int(speed) ) == ERROR )
    {
	lprintf( L_FATAL, "cannot get terminal line dev=%s, exiting", Device);
	//exit(30);
        Exit(30);
	exit(30);
    }
//------------------------ OpenCom end -----------------------------------
*/
#ifdef TIO_SET_MMC
	SwitchOnModule();
#endif



    /* drain input - make sure there are no leftover "NO CARRIER"s
     * or "ERROR"s lying around from some previous dial-out
     */

    clean_line( STDIN, 1);

    SendAT(); //For Autobauding

    /* do modem initialization, normal stuff first, then fax
     */

    if ( c_bool(direct_line) )
        Connect = "DIRECT";		/* for "\I" in issue/prompt */
    else
    {
	/* initialize data part */
	lprintf( L_MESG, "Initialisiere GSM-Modem!");
	mdm_lock_tty();
	if ( mg_init_data( STDIN, c_chat(init_chat), c_bool(need_dsr),
	                          c_chat(force_init_chat) ) == FAIL )
	{
	    mdm_release_tty();
	    lprintf( L_AUDIT, "failed in mg_init_data, dev=%s, pid=%d",
	                      Device, getpid() );

#if 0
	    //----- Try at 115200 -------
	    CloseCom();
	    lprintf( L_AUDIT, "Switching to speed 115200!");
            OpenCom(uid,gid,devname,1,115200);
#else
#warning using 57600
	    //----- Try at 57600 -------
	    CloseCom();
	    lprintf( L_AUDIT, "Switching to speed 57600!");
	    OpenCom(uid,gid,devname,1,57600);
#endif
	    mdm_lock_tty();
	    if ( mg_init_data( STDIN, c_chat(force_init_chat), c_bool(need_dsr),NULL ) == FAIL )
		{   mdm_release_tty();
		    tio_flush_queue( STDIN, TIO_Q_BOTH );	/* unblock flow ctrl */
		    rmlocks();
		    //exit(1);
		    lprintf( L_MESG, "Initialisierung fehlgeschlagen!");
#ifdef MODEM_RESET
		    SwitchOffModule();
#endif
		    Exit(1);
		    exit(1);
		}
	    mdm_release_tty();
	    CloseCom();
	    lprintf( L_AUDIT, "Switching to normal speed!");
            OpenCom(uid,gid,devname,0,0);
	    //--------------------------
	}
	else
	 {mdm_release_tty();
#ifdef MODEM_RESET
	  ClearModemResetCnt();
	 }
#endif



	EnterPin(NULL);
	SetSpeed(75);



/*

	if ( mg_init_data( STDIN, c_chat(cimi_chat), c_bool(need_dsr),
	                          NULL ) == FAIL )

	{
	 if ( mg_init_data( STDIN, c_chat(pin_chat), c_bool(need_dsr),
	                          NULL ) == FAIL )
		{lprintf( L_AUDIT, "failed in mg_init_data, dev=%s, pid=%d", Device, getpid() );
	         tio_flush_queue( STDIN, TIO_Q_BOTH );	// unblock flow ctrl
	         rmlocks();
	         //exit(1);
	         Exit(1);
		}

	}
*/

	/* if desired, get some "last call statistics" info */
	if ( c_isset(statistics_chat) )
	{
	    get_statistics( STDIN, c_chat(statistics_chat),
		 c_isset(statistics_file)? c_string(statistics_file): NULL );
	}

	/* initialize ``normal'' fax functions */
	if ( ( ! c_bool(data_only) ) &&
	     strcmp( c_string(modem_type), "data" ) != 0 &&
	     mg_init_fax( STDIN, c_string(modem_type),
			  c_string(station_id), c_bool(fax_only),
			  c_int(fax_max_speed) ) == SUCCESS )
	{
	    /* initialize fax polling server (only if faxmodem) */
	    if ( c_isset(fax_server_file) )
	    {
		faxpoll_server_init( STDIN, c_string(fax_server_file) );
	    }
	}

#ifdef VOICE
    voice_fd = STDIN;
    voice_init();

    if ( use_voice_mode ) {
	/* With external modems, the auto-answer LED can be used
	 * to show a status flag. vgetty uses this to indicate
	 * that new messages have arrived.
	 */
	vgetty_message_light();
    }
#endif /* VOICE */

	/* some modems forget some of their settings during fax/voice
	 * initialization -- use this as 'last chance' to set those things
	 * [don't care for errors here]
	 */
	if ( c_isset( post_init_chat ) )
	{
	    lprintf( L_NOISE, "running post_init_chat" );
	    do_chat( STDIN, c_chat(post_init_chat), NULL, NULL, 10, TRUE );
	}
    }

    /* wait .3s for line to clear (some modems send a \n after "OK",
       this may confuse the "call-chat"-routines) */

    clean_line( STDIN, 3);

    /* remove locks, so any other process can dial-out. When waiting
       for "RING" we check for foreign lockfiles, if there are any, we
       give up the line - otherwise we lock it again */

    rmlocks();

#if ( defined(linux) && defined(NO_SYSVINIT) ) || defined(sysV68)
    /* on linux, "simple init" does not make a wtmp entry when you
     * log so we have to do it here (otherwise, "who" won't work) */
    make_utmp_wtmp( Device, UT_INIT, "uugetty", NULL );
#endif

    /* sleep... waiting for activity */
    //mgetty_state = St_waiting;
    mgetty_state = St_GSM_data;

    //GSM_State(GSM_READY);
    GSM_State(GSM_NO_NETWORK);
    time(&StartTime);

    while ( mgetty_state != St_get_login &&
	    mgetty_state != St_callback_login )
    {

	//lprintf( L_WARN, "State:%d", mgetty_state  );
	switch (mgetty_state)	/* state machine */
	{/*
	  case St_MMC_Card_active:	Shared_GSM_Data->MMC_Status=1; //MMC enable
					lprintf( L_MESG, "### MMC access enabled! MMC_Status = %d ###",Shared_GSM_Data->MMC_Status);
					while(Shared_GSM_Data->MMC_Request != 0)
					 wait(100);
					Shared_GSM_Data->MMC_Status=0; //MMC disable
					lprintf( L_MESG, "### MMC access disbled! MMC_Status = %d ###",Shared_GSM_Data->MMC_Status);
					mgetty_state = St_GSM_Signal;
					break;

		*/
          case St_go_to_jail:
	    /* after a rejected call (caller ID, not enough RINGs,
	     * /etc/nologin file), do some cleanups, and go back to
	     * field one: St_waiting
	     */
	    //-------------- Releasing Modem line ------------------
	    /*
	    lprintf( L_MESG, "Releasing Modem line for 2 seconds!");
	    releaseMMCModem( RESOURCE_INDEX_MODEM );
	    sleep(2);
	    lprintf(L_FATAL,"Requesting Modem Line");
	    if( (i = requestMMCModem( RESOURCE_INDEX_MODEM, 0 )) <= 0)
	     {lprintf(L_FATAL,"main() -> Serial line busy!");
	      mgetty_state = St_go_to_jail;
	      break;
	     }
	    */
	    //-------------- Releasing Modem line end --------------

	    CallTime = CallName = CalledNr = "";	/* dirty */
	    CallerId = "none";
	    clean_line( STDIN, 3);			/* let line settle */
	    rmlocks();
	    mgetty_state = St_waiting;
	    break;

	  case St_waiting:
	    /* wait for incoming characters (using select() or poll() to
	     * prevent eating away from processes dialing out)
	     */
	    /*
	    lprintf( L_NOISE, "Checking MMC Request -> %d",Shared_GSM_Data->MMC_Request);
	    if(Shared_GSM_Data->MMC_Request == 1)
             {mgetty_state = St_MMC_Card_active;
	      break;
	     }
	   */

	   lprintf( L_MESG, "waiting...%d s" , wait_time/1000);
	    //GSM_State(GSM_READY);
	    /* ignore accidential sighup, caused by dialout or such
	     */
	    signal( SIGHUP, SIG_IGN );

	    /* here's mgetty's magic. Wait with select() or something
	     * similar non-destructive for activity on the line.
	     * If called with "-b" or as "getty", the blocking has
	     * already happened in the open() call.
	     */
	    if ( ! c_bool(blocking) )
	    {
		//int wait_time = c_int(modem_check_time)*1000;
		//int wait_time = c_int(modem_check_time)*10;
		//int wait_time = 30 * 1000;

		if ( ! wait_for_input( STDIN, wait_time ) &&
		     ! c_bool(direct_line) && ! virtual_ring )
		{
		    /* no activity - is the modem alive or dead? */
		    log_close();
		    //mgetty_state = St_GSM_Signal; //vor MMC
		    mgetty_state = St_GSM_Release_tty; // War vorher St_GSM_Signal
		    /*
		    lprintf( L_NOISE, "Checking MMC Request -> %d",Shared_GSM_Data->MMC_Request);
		    if(Shared_GSM_Data->MMC_Request == 1)
		     mgetty_state = St_MMC_Card_active;
		    else
		     mgetty_state = St_GSM_Signal;
		    */
		    break;
		}
	    }

	    /* close (and reopen) log file, to make sure it hasn't been
	     * moved away while sleeping and waiting for 'activity'
	     */
	    log_close();

	    /* check for LOCK files, if there are none, grab line and lock it
	     */


	    lprintf( L_NOISE, "checking lockfiles, locking the line" );

	    if ( makelock(Device) == FAIL)
	    {
		lprintf( L_NOISE, "lock file exists (dialout)!" );
		mgetty_state = St_dialout;
		break;
	    }

	    /* now: honour SIGHUP
	     */
	    signal(SIGHUP, sig_goodbye );

	    rings = 0;

	    /* check, whether /etc/nologin.<device> exists. If yes, do not
	       answer the phone. Instead, wait for ringing to stop. */
#ifdef NOLOGIN_FILE

	    sprintf( buf, NOLOGIN_FILE, DevID );

	    if ( access( buf, F_OK ) == 0 )
	    {
		lprintf( L_MESG, "%s exists - do not accept call!", buf );
		mgetty_state = St_nologin;
		break;
	    }
#endif
	    mgetty_state = St_wait_for_RINGs;
	    //mgetty_state = St_GSM_data;
	    break;


	  case St_check_modem:
	    /* some modems have the nasty habit of just dying after some
	       time... so, mgetty regularily checks with AT...OK whether
	       the modem is still alive */

	    //Shared_GSM_Data->SMSResult=-1;
	    //strcpy(Shared_GSM_Data->SMSMessage,"Test");
	    //strcpy(Shared_GSM_Data->SMSCalledNumber,"01722633279");
            if((Shared_GSM_Data->SMSResult==-1) )
             {lprintf( L_MESG, "************* SMS (Start) *******************" );
              if((strlen(Shared_GSM_Data->SMSMessage)>0) && (strlen(Shared_GSM_Data->SMSMessage)<=160))
               {lprintf( L_MESG, "Tel: %s [%s]",Shared_GSM_Data->SMSCalledNumber,Shared_GSM_Data->SMSMessage );
	        lprintf( L_MESG, "---------------------------------------------" );
	        if(Shared_GSM_Data->Status==GSM_READY)
                 {if(Send_SMS(Shared_GSM_Data->SMSCalledNumber, Shared_GSM_Data->SMSMessage , 0)==NOERROR)
	           {Shared_GSM_Data->SMSResult=1;	//OK
                    lprintf( L_MESG, "Shared_GSM_Data->SMSResult=1");
	           }
	          else
                   {Shared_GSM_Data->SMSResult=0;  	//Error
		    lprintf( L_MESG, "Shared_GSM_Data->SMSResult=0");
		   }
	         }
	        else
                 lprintf( L_MESG, "Waiting! Modem is not ready!" );
	       }
              else
	       {Shared_GSM_Data->SMSResult=0;  	//Error
	        lprintf( L_MESG, "SMS not sent -> strlen = %d",strlen(Shared_GSM_Data->SMSMessage));
                lprintf( L_MESG, "Shared_GSM_Data->SMSResult=%d",Shared_GSM_Data->SMSResult);
               }
	      lprintf( L_MESG, "************* SMS (Ende) ********************" );
	     }

	    lprintf( L_MESG, "checking if modem is still alive" );

	    if ( makelock( Device ) == FAIL )
	    {
		mgetty_state = St_dialout; break;
	    }

	    /* try twice */
	    if ( mdm_command( "AT", STDIN ) == SUCCESS ||
		 mdm_command( "AT", STDIN ) == SUCCESS )
	    {
		mgetty_state = St_go_to_jail; break;
	    }

	    lprintf( L_FATAL, "modem on dev=%s doesn't react!", Device );

	    /* give up */
	    //exit(30);
	    Exit(30);
	    exit(30);
	    break;

	  case St_nologin:
#ifdef NOLOGIN_FILE
	    /* if a "/etc/nologin.<device>" file exists, wait for RINGing
	       to stop, but count RINGs (in case the user removes the
	       nologin file while the phone is RINGing), and if the modem
	       auto-answers, handle it properly */

	    sprintf( buf, NOLOGIN_FILE, DevID );

	    /* while phone is ringing... */

	    while( wait_for_ring( STDIN, NULL, 10, ring_chat_actions,
				  &what_action, &dist_ring ) == SUCCESS )
	    {
		rings++;
		if ( access( buf, F_OK ) != 0 ||	/* removed? */
		     virtual_ring == TRUE )		/* SIGUSR1? */
		{
		    mgetty_state = St_wait_for_RINGs;	/* -> accept */
		    //mgetty_state = St_GSM_data;
		    break;
		}
	    }

	    /* did nologin file disappear? */
	    if ( mgetty_state != St_nologin ) break;

	    /* phone stopped ringing (do_chat() != SUCCESS) */
	    switch( what_action )
	    {
	      case A_TIMOUT:	/* stopped ringing */
		lprintf( L_AUDIT, "rejected, rings=%d", rings );
		mgetty_state = St_go_to_jail;
		break;
	      case A_CONN:	/* CONNECT */
		clean_line( STDIN, 5 );
		printf( "\r\n\r\nSorry, no login allowed\r\n" );
		printf( "\r\nGoodbye...\r\n\r\n" );
		sleep(5); /*exit(20);*/Exit(20);exit(20); break;
	      case A_FAX:	/* +FCON */
		mgetty_state = St_incoming_fax; break;
	      default:
		lprintf( L_MESG, "unexpected action: %d", what_action );
		//exit(20);
	        Exit(20);
		exit(20);
	    }
#endif
	    break;


	  case St_dialout:
	    /* wait for lock file to disappear *OR* for callback in progress */
	    mgetty_state = st_dialout(devname);
	    break;

	  case St_GSM_NETSEARCH:
            //Provider
	   lprintf( L_MESG, "###### Netzsuche! #######" );
	   strcpy(a,"AT+COPS?");
	   pa = mdm_get_idstring(&a[0], 1, STDIN );
           if(strncmp(pa,"+COPS:",6) == 0)
	    {if( (pchar1 = strchr(pa,'"')) != NULL)
	      { if((pchar2 = strrchr(pa,'"')) != NULL)
		 {if(pchar2 > (pchar1+1))
		   {strncpy(Shared_GSM_Data->Provider,pchar1+1,pchar2-(pchar1+1));
		    if(strlen(Shared_GSM_Data->Provider)==0)
		     strcpy(Shared_GSM_Data->Provider,"?");
                   }
		  else
                   strcpy(Shared_GSM_Data->Provider,"?");
                 }
	        else
                 strcpy(Shared_GSM_Data->Provider,"?");
	      }
	     else
              strcpy(Shared_GSM_Data->Provider,"?");
            }
	   else
            strcpy(Shared_GSM_Data->Provider,"?");

	   if(strcmp(Shared_GSM_Data->Provider,"?") == 0)
	    {GSM_State(GSM_NO_NETWORK);
	     mgetty_state = St_GSM_NETSEARCH;
	     lprintf( L_MESG, "--> Provoider : %s (wait_time=%d s)", Shared_GSM_Data->Provider , wait_time );
	     //wait(3);
	     sleep(3);
	     break;
	    }
	   else
	    {GSM_State(GSM_READY);
	    }
	   lprintf( L_MESG, "--> Provoider : %s (wait_time=%d s)", Shared_GSM_Data->Provider , wait_time );

	   strcpy(a,"AT+CSQ");
	    pa = mdm_get_idstring(&a[0], 1, STDIN );
	    Wert1=1;Wert2=1;
	    if( (*pa == '+') && (*(pa+1) == 'C') && (*(pa+2) == 'S') && (*(pa+3) == 'Q') && (*(pa+4) == ':') && (*(pa+5) == ' ') )
	     {sscanf(pa, "+CSQ:%d,%d", &Wert1, &Wert2);

	      if( (Wert1 > 1) && (Wert1 < 31))
	      Empfang = ((double)Wert1 * (99.0/28.0)) -  ((2.0*(99.0/28.0)) - 1.0);
              else
	       {if(Wert1 < 2)
	         Empfang=0.0;
                else
	         Empfang = 100.0;
	       }

              if((Shared_GSM_Data->Status == GSM_READY) || (Shared_GSM_Data->Status == GSM_NO_SIM) || (Shared_GSM_Data->Status == GSM_NO_NETWORK))
               Shared_GSM_Data->Signal = (int) Empfang;
              else
 	       Shared_GSM_Data->Signal = 0;
	      lprintf( L_MESG, "GSM: %s %d %d (Empfang = %.0f %%)(%d)", pa, Wert1, Wert2 , Empfang, Shared_GSM_Data->Signal );
	    }
	    mgetty_state = St_go_to_jail;
	   break;


	  case St_GSM_data:

	   OperatorSelection();

	   strcpy(a,"ATI");
	   pa = mdm_get_idstring(&a[0], 2, STDIN );

	   strcpy(Shared_GSM_Data->Name, pa);
           lprintf( L_MESG, "GSM Name: %s", Shared_GSM_Data->Name );

	   pa = mdm_get_idstring(&a[0], 3, STDIN );

	   strcpy(Shared_GSM_Data->Version, pa);
           sscanf(pa,"%s %s",a,aa);
	   sprintf(Shared_GSM_Data->Version,"%s [%s]",aa,Shared_GSM_Data->Name);
	   lprintf( L_MESG, "GSM Version: %s", Shared_GSM_Data->Version );

           //IMEI
	   strcpy(a,"AT+CGSN");
	   pa = mdm_get_idstring(&a[0], 1, STDIN );

	   strcpy(Shared_GSM_Data->IMEI, pa);
           lprintf( L_MESG, "--> IMEI : %s", pa );

	   //Service centre address
	   char *t1,*t2;

	   strcpy(a,"AT+CSCA?");
	   pa = mdm_get_idstring(&a[0], 1, STDIN );
	   strcpy(Shared_GSM_Data->ServiceCentre,"\0");
	   if(strstr(pa,"+CSCA: ") != NULL)
	    {if( (t1 = strchr(pa,'"')) != NULL)
	      {if( (t2 = strchr(t1+1,'"')) != NULL)
	        {if( ((t2 - t1 -1) > 0) && ((t2 - t1 -1) <= 20))
		  {strncpy(Shared_GSM_Data->ServiceCentre,t1+1,t2-t1-1);
		   lprintf( L_MESG, "Service centre address : %s", Shared_GSM_Data->ServiceCentre);
		  }
		}
	      }
	    }

	   //SIM card number
	   strcpy(a,"AT^SCID");
	   pa = mdm_get_idstring(&a[0], 1, STDIN );
	   if(strstr(pa,"^SCID: ") != NULL)
	    {if(strlen(pa) >= 8)
              {memset(Shared_GSM_Data->SIM_ID,0,30);
               strncpy(Shared_GSM_Data->SIM_ID, pa+7,29);
              }
            }
	   else
	    strcpy(Shared_GSM_Data->SIM_ID,"\0");
           lprintf( L_MESG, "SIM ID: %s", Shared_GSM_Data->SIM_ID);


	   //Provider
	   strcpy(a,"AT+COPS?");
	   //strcpy(Shared_GSM_Data->Provider,"?");
	   pa = mdm_get_idstring(&a[0], 1, STDIN );
           if(strncmp(pa,"+COPS:",6) == 0)
	    {if( (pchar1 = strchr(pa,'"')) != NULL)
	      { if((pchar2 = strrchr(pa,'"')) != NULL)
		 {if(pchar2 > (pchar1+1))
		   {strncpy(Shared_GSM_Data->Provider,pchar1+1,pchar2-(pchar1+1));
		    if(strlen(Shared_GSM_Data->Provider)==0)
		     strcpy(Shared_GSM_Data->Provider,"?");
		   }
                  else
		   strcpy(Shared_GSM_Data->Provider,"?");
		 }
                else
		 strcpy(Shared_GSM_Data->Provider,"?");
	      }
	     else
	      strcpy(Shared_GSM_Data->Provider,"?");
	    }
	   else
	    strcpy(Shared_GSM_Data->Provider,"?");

	   if(Shared_GSM_Data->Status == GSM_PIN_ERROR)
            {mgetty_state = St_GSM_Wrong_PIN;
	     lprintf( L_MESG, "### Pin Falsch -> Warte auf neue Pin!!! ###");
	     break;
            }

	   lprintf( L_MESG, "--> Provoider : %s (wait_time=%d s)", Shared_GSM_Data->Provider , wait_time);

           if(strcmp(Shared_GSM_Data->Provider,"?") == 0)
	    {GSM_State(GSM_NO_NETWORK);
 	     //mgetty_state = St_GSM_NETSEARCH;
	     //break;
	    }
	   else
	    {char G;
	     char BCCH[20];

	     //GSM_State(GSM_READY);

	     GPRS_Enabled = GPRS_enabled();
	     GPRS_Available = 0;

	     if(GPRS_Enabled == 1)
			  { int gprs_state=0;

					lprintf( L_NOISE, "### Checking for GPRS attached... [1]" );

					strcpy(a,"AT+CGATT?");
					pa = mdm_get_idstring(&a[0], 1, STDIN );

					if(sscanf(pa,"+CGATT: %d",&gprs_state) == 1)
					{
						 if(gprs_state == 1) //GPRS attached
						 { lprintf( L_NOISE, "### GPRS attached -> detach! State=%d [1]", gprs_state);
							 strcpy(a,"AT+CGATT=0");
							 pa = mdm_get_idstring(&a[0], 1, STDIN );
						 }
						 else
							 lprintf( L_NOISE, "### GPRS detached! State=%d [1]", gprs_state);
					 }
					 else
					  lprintf( L_NOISE, "Error parsing answer [1]" );

				 lprintf( L_NOISE, "### Checking for GPRS service...[1]" );
	       strcpy(a,"AT^SMONG");
	       pa = mdm_get_idstring(&a[0], 3, STDIN );
	       if(sscanf(pa,"%s %c",&BCCH[0],&G) != EOF)
				 {if((G == '1') || (G == '3')) //1=GPRS, 3=EGPRS available in cell
		  {lprintf( L_NOISE, "### GPRS service available! BCCH=%s , G=%c",BCCH,G );
		   GPRS_Available=1;
		   GSM_State(GSM_READY);
		  }
	         else
		  {GSM_State(GSM_GPRS_NOT_AVAILABLE);
		   lprintf( L_NOISE, "### GPRS service not available! BCCH=%s , G=%c",BCCH,G );
		  }
	        }
	       else
	        {lprintf( L_NOISE, "### Error! GPRS service not clear -> try to connect(%s)",pa );
	         GSM_State(GSM_READY);
	         GPRS_Available=1;
	        }
              }
	     else
	      GSM_State(GSM_READY);
	    }



	    //Signalstaerke
	    strcpy(a,"AT+CSQ");
	    pa = mdm_get_idstring(&a[0], 1, STDIN );
	    Wert1=1;Wert2=1;
	    if( (*pa == '+') && (*(pa+1) == 'C') && (*(pa+2) == 'S') && (*(pa+3) == 'Q') && (*(pa+4) == ':') && (*(pa+5) == ' ') )
	     {sscanf(pa, "+CSQ:%d,%d", &Wert1, &Wert2);

	      if( (Wert1 > 1) && (Wert1 < 31))
	      Empfang = ((double)Wert1 * (99.0/28.0)) -  ((2.0*(99.0/28.0)) - 1.0);
              else
	       {if(Wert1 < 2)
	         Empfang=0.0;
                else
	         Empfang = 100.0;
	       }

              if((Shared_GSM_Data->Status == GSM_READY) || (Shared_GSM_Data->Status == GSM_NO_SIM) || (Shared_GSM_Data->Status == GSM_NO_NETWORK))
               Shared_GSM_Data->Signal = (int) Empfang;
	      else
               Shared_GSM_Data->Signal = 0;
              lprintf( L_MESG, "GSM: %s %d %d (Empfang = %.0f %%)(%d)", pa, Wert1, Wert2 , Empfang, Shared_GSM_Data->Signal );
	    }

	   mgetty_state = St_go_to_jail;
           break;
//-----------------------------------------------------------------------------------------
	  case St_GSM_Wrong_PIN:
		if(Shared_GSM_Data->Status == GSM_PIN_ERROR)
	         {if(Shared_GSM_Data->NewPin == 1)
		   {lprintf( L_MESG, "### Enter new Pin!" );
		    GSM_State(GSM_INIT);
		    EnterPin(Shared_GSM_Data->setPin);
		   }
                  else
		   {mgetty_state = St_GSM_Wrong_PIN_Release_tty;
		    //sleep(1);
		   }
                 }
		else
		 mgetty_state = St_GSM_data;
		break;

	  case St_GSM_Wrong_PIN_Release_tty:
	    Shared_GSM_Data->MMC_Status=0; // 1 -> 0 : 23.10.08
	    lprintf( L_MESG, "Releasing Modem line for 2 seconds! MMC_Status=%d (WRONG_PIN!)",Shared_GSM_Data->MMC_Status);
	    CloseCom();
	    releaseMMCModem( RESOURCE_INDEX_MODEM );
	    sleep(2);
	    mgetty_state = St_GSM_Wrong_PIN_Request_tty;
	   break;

	  case St_GSM_Wrong_PIN_Request_tty:
	    if(Shared_GSM_Data->NewPin == 0)
	     {mgetty_state = St_GSM_Wrong_PIN_Request_tty;
	      sleep(2);
	      break;
	     }
	    lprintf(L_MESG,"Requesting Modem Line (WRONG_PIN!)");
	    if( (i = requestMMCModem( RESOURCE_INDEX_MODEM, 0 )) <= 0)
	     {lprintf(L_MESG,"main() -> Serial line busy! (WRONG_PIN!)");
	      mgetty_state = St_GSM_Wrong_PIN_Request_tty;
	      sleep(2);
	      break;
	     }
	    else
             {Shared_GSM_Data->MMC_Status=0;
	      lprintf(L_MESG,"main() -> Serial line free! MMC_Status=%d (WRONG_PIN!)",Shared_GSM_Data->MMC_Status);
	      OpenCom(uid,gid,devname,0,0);
	      mgetty_state = St_GSM_Wrong_PIN;
	     }
	   break;
//--------------------------------------------------------------------------------------
	  case St_GSM_Release_tty:

	    if(Shared_GSM_Data->SMSResult >= 0) //No SMS request
             {Shared_GSM_Data->MMC_Status=0; // 1 -> 0 : 23.10.08
	      lprintf( L_MESG, "Releasing Modem line for 2 seconds! MMC_Status=%d",Shared_GSM_Data->MMC_Status);
	      CloseCom();
	      releaseMMCModem( RESOURCE_INDEX_MODEM );
	      sleep(2);
	      mgetty_state = St_GSM_Request_tty;
	     }
	    else
             {mgetty_state = St_GSM_CheckSimStatus;
	      lprintf( L_MESG, "*** No modem release! (SMS) ***");
	     }
	   break;



          case St_GSM_Request_tty:
		lprintf(L_MESG,"Requesting Modem Line");
	        if( (i = requestMMCModem( RESOURCE_INDEX_MODEM, 0 )) <= 0)
	         {lprintf(L_MESG,"main() -> Serial line busy!");
	          mgetty_state = St_GSM_Request_tty;
	          sleep(2);
		  //Exit(1);
	          break;
	         }
	        else
                 {Shared_GSM_Data->MMC_Status=0;
	          lprintf(L_MESG,"main() -> Serial line free! MMC_Status=%d",Shared_GSM_Data->MMC_Status);
	          mgetty_state = St_GSM_CheckSimStatus;
		 }
	      OpenCom(uid,gid,devname,0,0);
	      break;

	case St_GSM_CheckSimStatus:
		if(Shared_GSM_Data->Status != GSM_READY)
		 SimStateCheck();
		mgetty_state = St_GSM_Signal;
		break;

	 case St_GSM_Signal:
	  //Service Centre
	  if(strlen(Shared_GSM_Data->ServiceCentre) == 0)
	    {char *t1,*t2;
	     strcpy(a,"AT+CSCA?");
	     pa = mdm_get_idstring(&a[0], 1, STDIN );
	     strcpy(Shared_GSM_Data->ServiceCentre,"\0");
	     if(strstr(pa,"+CSCA: ") != NULL)
	      {if( (t1 = strchr(pa,'"')) != NULL)
	        {if( (t2 = strchr(t1+1,'"')) != NULL)
	          {if( ((t2 - t1 -1) > 0) && ((t2 - t1 -1) <= 20))
		    {strncpy(Shared_GSM_Data->ServiceCentre,t1+1,t2-t1-1);
		     lprintf( L_MESG, "Service centre address : %s", Shared_GSM_Data->ServiceCentre);
		    }
		  }
	        }
	      }
	    }

	   //SIM card number
	   strcpy(a,"AT^SCID");
	   pa = mdm_get_idstring(&a[0], 1, STDIN );
	   if(strstr(pa,"^SCID: ") != NULL)
	    {if(strlen(pa) >= 8)
              {memset(Shared_GSM_Data->SIM_ID,0,30);
               strncpy(Shared_GSM_Data->SIM_ID, pa+7,29);
              }
            }
	   else
	    strcpy(Shared_GSM_Data->SIM_ID,"\0");
           lprintf( L_MESG, "SIM ID: %s", Shared_GSM_Data->SIM_ID);


	  //GSM module version
	  if((Shared_GSM_Data->Name[0] != 'M') || (Shared_GSM_Data->Name[1] != 'C'))
	   {strcpy(a,"ATI");
	    pa = mdm_get_idstring(&a[0], 2, STDIN );

	    strcpy(Shared_GSM_Data->Name, pa);
            lprintf( L_MESG, "GSM Name: %s", Shared_GSM_Data->Name );

	    pa = mdm_get_idstring(&a[0], 3, STDIN );

	    strcpy(Shared_GSM_Data->Version, pa);
            sscanf(pa,"%s %s",a,aa);
	    sprintf(Shared_GSM_Data->Version,"%s [%s]",aa,Shared_GSM_Data->Name);
	    lprintf( L_MESG, "GSM Version: %s", Shared_GSM_Data->Version );
	   }

	   //Provider

	  OperatorSelection();

	   strcpy(a,"AT+COPS?");
	   //strcpy(Shared_GSM_Data->Provider,"?");
	   pa = mdm_get_idstring(&a[0], 1, STDIN );
           if(strncmp(pa,"+COPS:",6) == 0)
	    {if( (pchar1 = strchr(pa,'"')) != NULL)
	      { if((pchar2 = strrchr(pa,'"')) != NULL)
		 {if(pchar2 > (pchar1+1))
		   {strncpy(Shared_GSM_Data->Provider,pchar1+1,pchar2-(pchar1+1));
		    if(strlen(Shared_GSM_Data->Provider)==0)
		     strcpy(Shared_GSM_Data->Provider,"?");
		   }
		  else
		   strcpy(Shared_GSM_Data->Provider,"?");
		 }
                else
		 strcpy(Shared_GSM_Data->Provider,"?");
	      }
	     else
              strcpy(Shared_GSM_Data->Provider,"?");
	    }
	   else
	    strcpy(Shared_GSM_Data->Provider,"?");

	   lprintf( L_MESG, "--> Provoider : %s", Shared_GSM_Data->Provider );

	   GPRS_Enabled = GPRS_enabled();
	   GPRS_Available = 0;

	   if(strcmp(Shared_GSM_Data->Provider,"?") == 0)
	    GSM_State(GSM_NO_NETWORK);
	   else //GPRS available?
	    {char G;
	     char BCCH[20];
			 int gprs_state=0;

			 lprintf( L_NOISE, "### Checking for GPRS attached... [2]" );

			 strcpy(a,"AT+CGATT?");
			 pa = mdm_get_idstring(&a[0], 1, STDIN );

			 if(sscanf(pa,"+CGATT: %d",&gprs_state) == 1)
			 {
				 if(gprs_state == 1) //GPRS attached
				 { lprintf( L_NOISE, "### GPRS attached -> detach! State=%d [2]", gprs_state);
					 strcpy(a,"AT+CGATT=0");
					 pa = mdm_get_idstring(&a[0], 1, STDIN );
				 }
				 else
					 lprintf( L_NOISE, "### GPRS detached! State=%d [2]", gprs_state);
			 }
			 else
				 lprintf( L_NOISE, "Error parsing answer [2]" );

	     //GSM_State(GSM_READY);

	     lprintf( L_NOISE, "### Checking for GPRS service...[2]" );
	     strcpy(a,"AT^SMONG");
	     pa = mdm_get_idstring(&a[0], 3, STDIN );

	     if(sscanf(pa,"%s %c",&BCCH[0],&G) != EOF)
			 {if((G == '1') || (G == '3')) //1=GPRS, 3=EGPRS available in cell
		{lprintf( L_NOISE, "### GPRS service available! BCCH=%s , G=%c",BCCH,G );
		 GPRS_Available=1;
		 GSM_State(GSM_READY);
		}
	       else
		{if(GPRS_Enabled == 1)
		  GSM_State(GSM_GPRS_NOT_AVAILABLE);
		 else
		  GSM_State(GSM_READY);
		 lprintf( L_NOISE, "### GPRS service not available! BCCH=%s , G=%c",BCCH,G );
		}
	      }
	     else
	      {lprintf( L_NOISE, "### Error! GPRS service not clear -> try to connect(%s)",pa );
	       GSM_State(GSM_READY);
	       GPRS_Available=1;
	      }
 	    }

	    //Signalstaerke
	    strcpy(a,"AT+CSQ");
	    pa = mdm_get_idstring(&a[0], 1, STDIN );
	    Wert1=1;Wert2=1;
	    if( (*pa == '+') && (*(pa+1) == 'C') && (*(pa+2) == 'S') && (*(pa+3) == 'Q') && (*(pa+4) == ':') && (*(pa+5) == ' ') )
	     {sscanf(pa, "+CSQ:%d,%d", &Wert1, &Wert2);

	      if( (Wert1 > 1) && (Wert1 < 31))
	      Empfang = ((double)Wert1 * (99.0/28.0)) -  ((2.0*(99.0/28.0)) - 1.0);
              else
	       {if(Wert1 < 2)
	         Empfang=0.0;
                else
	         Empfang = 100.0;
	       }

              if((Shared_GSM_Data->Status == GSM_READY) || (Shared_GSM_Data->Status == GSM_NO_SIM) || (Shared_GSM_Data->Status == GSM_NO_NETWORK))
               Shared_GSM_Data->Signal = (int) Empfang;
              else
	       Shared_GSM_Data->Signal = 0;
	      lprintf( L_MESG, "GSM: %s %d %d (Empfang = %.0f %%)(%d)", pa, Wert1, Wert2 , Empfang, Shared_GSM_Data->Signal );
	    }

	     //****
             CallTime = CallName = CalledNr = "";	/* dirty */
	     CallerId = "none";
	     clean_line( STDIN, 3);			/* let line settle */
	     rmlocks();
	     //mgetty_state = St_waiting;
             //****

	     //mgetty_state = St_check_modem;
	     mgetty_state = ST_GPRS_Connection;
	  break;

	 case ST_GPRS_Connection:

		//SoftReset if GPRS dial in failed
		if(GPRS_Enabled == 1)
		 CheckFailedGprsDialIn();

		if((GPRS_Available == 1) && (GPRS_Enabled == 1))
		 {int ret;
		  lprintf( L_NOISE, "Starting GPRS connection ..." );
                  GSM_State(GSM_GPRS_CALL);
		  ret = system("/etc/ppp/ppp.sh start");
		  lprintf( L_NOISE, "GPRS connection finished ret=%d -> Exit",ret);
		  Exit(30);
		 }
		else
		 lprintf( L_NOISE, "GPRS not started GPRS_Available=%d , GPRS_Enabled=%d!",GPRS_Available,GPRS_Enabled );

	        mgetty_state = St_check_modem;
		break;

	  case St_wait_for_RINGs:
	    /* Wait until the proper number of RING strings have been
	       seen. In case the modem auto-answers (yuck!) or someone
	       hits DATA/VOICE, we'll accept CONNECT, +FCON, ... also. */

	    //clean_line( STDIN, 1);
	    if ( c_bool(direct_line) )			/* no RING needed */
	    {
		mg_get_ctty( STDIN, devname );		/* get controll.tty */
		mgetty_state = St_get_login;
		break;
	    }

	    dist_ring=0;		/* yet unspecified RING type */

	    if ( c_bool(ringback) )	/* don't pick up on first call */
	    {
		int n = 0;

		while( wait_for_ring( STDIN, NULL, 17, ring_chat_actions,
				      &what_action, &dist_ring ) == SUCCESS &&
		        ! virtual_ring )
		{ n++; }

		if ( what_action != A_TIMOUT )
		 {goto Ring_got_action;
		 }
		lprintf( L_MESG, "ringback: phone stopped after %d RINGs, waiting for re-ring", n );
	    }

	    /* how many rings to wait for (normally) */
	    rings_wanted = c_int(rings_wanted);

#ifdef VOICE
	    if ( use_voice_mode ) {
		/* modify, if toll saver, or in vgetty answer-file */
		vgetty_rings(&rings_wanted);
	    }
#endif /* VOICE */

	    //rings_wanted = 20;
	    while ( rings < rings_wanted )
	    {
		if ( wait_for_ring( STDIN, c_chat(msn_list),
			  ( c_bool(ringback) && rings == 0 )
				? c_int(ringback_time) : ring_chat_timeout,
			  ring_chat_actions, &what_action,
			  &dist_ring ) == FAIL)
		{

		    break;		/* ringing stopped, or "action" */
		}
		GSM_State(GSM_CALL);

		rings++;
	    }

	    /* enough rings? */
	    if ( rings >= rings_wanted )
	    {   //GSM_State(GSM_CONNECTION);
		mgetty_state = St_answer_phone; break;
	    }

Ring_got_action:
	    /* not enough rings, timeout or action? */

	    switch( what_action )
	    {
	      case A_TIMOUT:		/* stopped ringing */
		if ( rings == 0 &&	/* no ring *AT ALL* */
		     ! c_bool(ringback))/* and not "missed" ringback */
		{
		    lprintf( L_WARN, "huh? Junk on the line?");
		    lprintf( L_WARN, " >>> could be a dial-out program without proper locking - check this!" );
		    rmlocks();		/* line is free again */
		    //exit(0);		/* let init restart mgetty */
		    Exit(0);
		    exit(0);
		}
		if ( c_bool(ringback) )
		    lprintf( L_AUDIT, "missed ringback!" );
		else
		    lprintf( L_AUDIT, "phone stopped ringing (rings=%d, dev=%s, pid=%d, caller='%s')", rings, Device, getpid(), CallerId );

		mgetty_state = St_go_to_jail;
		break;
	      case A_CONN:		/* CONNECT */
		mg_get_ctty( STDIN, devname );
		mgetty_state = St_get_login; break;
	      case A_FAX:		/* +FCON */
		mgetty_state = St_incoming_fax; break;
#ifdef VOICE
	      case A_VCON:
		vgetty_button(rings);
		use_voice_mode = FALSE;
		mgetty_state = St_answer_phone;
		break;
#endif
	      case A_FAIL:
		lprintf( L_AUDIT, "failed A_FAIL dev=%s, pid=%d, caller='%s'",
			          Device, getpid(), CallerId );
		//exit(20);
	        Exit(20);
		exit(20);
	      default:
		lprintf( L_MESG, "unexpected action: %d", what_action );
		//exit(20);
	        Exit(20);
		exit(20);
	    }
	    break;


	  case St_answer_phone:
	    /* Answer an incoming call, after the desired number of
	       RINGs. If we have caller ID information, and checking
	       it is desired, do it now, and possibly reject call if
	       not allowed in. If we have to do some chat with the modem
	       to get the Caller ID, do it now. */

	    if ( c_isset(getcnd_chat) )
	    {
		do_chat( STDIN, c_chat(getcnd_chat), NULL, NULL, 10, TRUE );
	    }

	    /* Check Caller ID.  Static table first, then cnd-program.  */

	    if ( !cndlookup() ||
	         ( c_isset(cnd_program) &&
		   cnd_call( c_string(cnd_program), Device, dist_ring ) == 1))
	    {
		lprintf( L_AUDIT, "denied caller dev=%s, pid=%d, caller='%s'",
			 Device, getpid(), CallerId);
		clean_line( STDIN, 80 ); /* wait for ringing to stop */

		mgetty_state = St_go_to_jail;
		break;
	    }

	    /* from here, there's no way back. Either the call will succeed
	       and mgetty will exec() something else, or it will fail and
	       mgetty will exit(). */

	    /* get line as ctty: hangup will come through
	     */
	    mg_get_ctty( STDIN, devname );

	    /* remember time of phone pickup */
	    call_start = time( NULL );

#ifdef VOICE
	    if ( use_voice_mode ) {
		int rc;
		/* Answer in voice mode. The function will return only if it
		   detects a data call, otherwise it will call exit(). */
		rc = vgetty_answer(rings, rings_wanted, dist_ring);

		/* The modem will be in voice mode when voice_answer is
		 * called. If the function returns, the modem is ready
		 * to be connected in DATA mode with ATA.
		 *
		 * Exception: a CONNECT has been seen (-> go to "login:")
		 *   or a fax connection is established (go to fax receive)
		 */

		if ( rc == VMA_CONNECT )
		    { mgetty_state = St_get_login; break; }
		if ( rc == VMA_FCO || rc == VMA_FCON || rc == VMA_FAX )
		    { mgetty_state = St_incoming_fax; break; }
	    }
#endif /* VOICE */

	    if ( do_chat( STDIN, c_chat(answer_chat), answer_chat_actions,
			 &what_action, c_int(answer_chat_timeout), TRUE)
			 == FAIL )
	    {
		if ( what_action == A_FAX )
		{
		    mgetty_state = St_incoming_fax;
		    break;
		}

		lprintf( L_AUDIT,
		  "failed %s dev=%s, pid=%d, caller='%s', conn='%s', name='%s'",
		    what_action == A_TIMOUT? "timeout": "A_FAIL",
		    Device, getpid(), CallerId, Connect, CallName );

		rmlocks();
		//exit(1);
	        Exit(1);
		exit(1);
	    }

	    /* some (old) modems require the host to change port speed
	     * to the speed returned in the CONNECT string, usually
	     * CONNECT 2400 / 1200 / "" (meaning 300)
	     */
	    if ( c_bool(autobauding) )
	    {
		int cspeed;

		if ( strlen( Connect ) == 0 )	/* "CONNECT\r" */
		    cspeed = 300;
		else
		    cspeed = atoi(Connect);

		lprintf( L_MESG, "autobauding: switch to %d bps", cspeed );

		if ( tio_check_speed( cspeed ) >= 0 )
		{				/* valid speed */
		    conf_set_int( &c.speed, cspeed );
		    tio_get( STDIN, &tio );
		    tio_set_speed( &tio, cspeed );
		    tio_set( STDIN, &tio );
		}
		else
		{
		    lprintf( L_ERROR, "autobauding: cannot parse 'CONNECT %s'",
			               Connect );
		}
	    }
	    GSM_State(GSM_CONNECTION);
	    mgetty_state = St_get_login;
	    break;

	  case St_incoming_fax:
	    /* incoming fax, receive it (->faxrec.c) */

	    lprintf( L_MESG, "start fax receiver..." );
	    get_ugid( &c.fax_owner, &c.fax_group, &uid, &gid );
	    faxrec( c_string(fax_spool_in), c_int(switchbd),
		    uid, gid, c_int(fax_mode),
		    c_isset(notify_mail)? c_string(notify_mail): NULL );

    /* some modems require a manual hangup, with a pause before it. Notably
       this is the creatix fax/voice modem, which is quite widespread,
       unfortunately... */

	    delay(1500);
	    mdm_command( "ATH0", STDIN );

	    rmlocks();
	    //exit( 0 );
	    Exit(0);
	    exit( 0 );
	    break;

	  default:
	    /* unknown machine state */

	    lprintf( L_WARN, "unknown state: %s", mgetty_state );
	    //exit( 33 );
	    Exit(33);
	    exit( 33 );
	}		/* end switch( mgetty_state ) */
    }			/* end while( state != St_get_login ) */

    /* this is "state St_get_login". Not included in switch/case,
       because it doesn't branch to other states. It may loop for
       a while, but it will never return
       */

    /* wait for line to clear (after "CONNECT" a baud rate may
       be sent by the modem, on a non-MNP-Modem the MNP-request
       string sent by a calling MNP-Modem is discarded here, too) */

    clean_line( STDIN, 3);

    tio_get( STDIN, &tio );
    /* honor carrier now: terminate if modem hangs up prematurely
     * (can be bypassed if modem / serial port broken)
     */
    if ( !c_bool( ignore_carrier ) )
    {
	tio_carrier( &tio, TRUE );
	tio_set( STDIN, &tio );
    }
    else
        lprintf( L_MESG, "warning: carrier signal is ignored" );

    /* make utmp and wtmp entry (otherwise login won't work)
     */
    make_utmp_wtmp( Device, UT_LOGIN, "LOGIN",
		      strcmp( CallerId, "none" ) != 0 ? CallerId: Connect );

    /* wait a little bit befor printing login: prompt (to give
     * the other side time to get ready)
     */
    delay( c_int(prompt_waittime) );

    /* loop until a successful login is made
     */
    for (;;)
    {
	/* protect against blocked output (for whatever reason) */
	signal( SIGALRM, sig_goodbye );
	alarm( 60 );

	/* set ttystate for /etc/issue ("before" setting) */
	gettermio(c_string(gettydefs_tag), TRUE, &tio);

	/* we have carrier, assert flow control (including HARD and IXANY!) */
	tio_set_flow_control( STDIN, &tio, DATA_FLOW | FLOW_XON_IXANY );
	tio_set( STDIN, &tio );

#ifdef NeXT
	/* work around NeXT's weird problems with POSIX termios vs. sgtty */
	NeXT_repair_line(STDIN);
#endif

	fputc('\r', stdout);	/* just in case */

	if (c_isset(issue_file))
	{
	    /* display ISSUE, if desired
	     */
	    lprintf( L_NOISE, "print welcome banner (%s)", c_string(issue_file));

	    if (c_string(issue_file)[0] == '!')		/* exec */
            {
                system( c_string(issue_file)+1 );
            }
            else if (c_string(issue_file)[0] != '/')
	    {
		printf( "%s\r\n", ln_escape_prompt( c_string(issue_file) ) );
	    }
	    else if ( (fp = fopen(c_string(issue_file), "r")) != (FILE *) NULL)
	    {
		while ( fgets(buf, sizeof(buf), fp) != (char *) NULL )
		{
		    char * p = ln_escape_prompt( buf );
		    if ( p != NULL ) fputs( p, stdout );
		    fputc('\r', stdout );
		}
		fclose(fp);
	    }
	}

	/* set permissions to "rw-------" for login */
	(void) chmod(devname, 0600);

	/* set ttystate for login ("after"),
	 *  cr-nl mapping flags are set by getlogname()!
	 */
#ifdef USE_GETTYDEFS
	gettermio(c_string(gettydefs_tag), FALSE, &tio);
	tio_set( STDIN, &tio );

	lprintf(L_NOISE, "i: %06o, o: %06o, c: %06o, l: %06o, p: %s",
		tio.c_iflag, tio.c_oflag, tio.c_cflag, tio.c_lflag,
		c_string(login_prompt));
#endif
	/* turn off alarm (getlogname has its own timeout) */
	alarm(0);

	/* read a login name from tty
	   (if just <cr> is pressed, re-print issue file)

	   also adjust ICRNL / IGNCR to characters recv'd at end of line:
	   cr+nl -> IGNCR, cr -> ICRNL, NL -> 0/ and: cr -> ONLCR, nl -> 0
	   for c_oflag */

	if ( getlogname( c_string(login_prompt), &tio, buf, sizeof(buf),
			 c_bool(blocking)? 0: c_int(login_time),
			 c_bool(do_send_emsi) ) == -1 )
	{
	     continue;
	}

	/* remove PID file (mgetty is due to exec() login) */
	(void) unlink( pid_file_name );

	/* dreadful hack for Linux, set TERM if desired */
	if ( c_isset(termtype) )
	{
	    char * t = malloc( 6 + strlen( c_string(termtype)) );
	    if ( t != NULL )
	        { sprintf( t, "TERM=%s", c_string(termtype) ); putenv(t); }
	}

	/* catch "standard question #29" (DCD low -> /bin/login gets stuck) */
	i = tio_get_rs232_lines(STDIN);
	if ( i != -1 && (( i & TIO_F_DCD ) == 0 ) )
	{
	    lprintf( L_WARN, "WARNING: starting login while DCD is low!" );
	}

	/* hand off to login dispatcher (which will call /bin/login) */
	login_dispatch( buf, mgetty_state == St_callback_login? TRUE: FALSE,
			c_string(login_config) );

	/* doesn't return, if it does, something broke */
	//exit(FAIL);
        Exit(FAIL);
	exit(FAIL);
    }
}

void
gettermio _P3 ((id, first, tio), char *id, boolean first, TIO *tio )
{
    char *rp;

#ifdef USE_GETTYDEFS
    static loaded = 0;
    GDE *gdp;
#endif

    /* default setting */
    if ( tio != NULL ) tio_mode_sane( tio, c_bool( ignore_carrier ) );
    rp = NULL;

#ifdef USE_GETTYDEFS
    /* if gettydefs used, override "tio_mode_sane" settings */

    if (!loaded)
    {
	if (!loadgettydefs(GETTYDEFS)) {
	    lprintf(L_WARN, "Couldn't load gettydefs - using defaults");
	}
	loaded = 1;
    }
    if ( (gdp = getgettydef(id)) != NULL )
    {
	lprintf(L_NOISE, "Using %s gettydefs entry, \"%s\"", gdp->tag,
		first? "before" : "after" );
	if (first)	/* "before" -> set port speed */
	{
	    if ( c.speed.flags == C_EMPTY ||	/* still default value */
		 c.speed.flags == C_PRESET )	/* -> use gettydefs */
	        conf_set_int( &c.speed, tio_get_speed( &(gdp->before)) );
	} else		/* "after" -> set termio flags *BUT NOT* speed */
            if ( tio != NULL )
	{
	    *tio = gdp->after;
	    tio_set_speed( tio, c_int(speed) );
	}
	rp = gdp->prompt;
    }

#endif

    if ( rp )		/* set login prompt only if still default */
    {
	if ( c.login_prompt.flags == C_EMPTY ||
	     c.login_prompt.flags == C_PRESET )
	{
	    c.login_prompt.d.p = (void *) rp;
	    c.login_prompt.flags = C_CONF;
	}
    }
}
