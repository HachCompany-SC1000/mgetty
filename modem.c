#ident "$Id: modem.c,v 4.4 1997/12/05 23:48:08 gert Exp $ Copyright (c) Gert Doering"

/* modem.c
 *
 * Module containing *very* basic modem functions
 *   - send a command
 *   - get a response back from the modem
 *   - send a command, wait for OK/ERROR response
 */

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "mgetty.h"
#include "policy.h"
#include "ttyResource.h"



void mdm_lock_tty _P0(void)
 {lprintf( L_MESG, "mdm_lock_tty()");
  kmuxLockModem();
 }

void mdm_release_tty _P0(void)
 {kmuxUnlockModem();
  lprintf( L_MESG, "mdm_release_tty()");
 }


/* get one line from the modem, only printable characters, terminated
 * by \r or \n. The termination character is *not* included
 */

char * mdm_get_line _P1( (fd), int fd )
{
    static char buffer[200];
    int bufferp;
    char c;

    bufferp = 0;
    lprintf( L_JUNK, "got:" );

    do
    {
	if( mdm_read_byte( fd, &c ) != 1 )
	{
	    lprintf( L_ERROR, "mdm_get_line: cannot read byte, return" );
	    return NULL;
	}

	lputc( L_JUNK, c );

	if ( isprint( c ) &&
	     bufferp < sizeof(buffer) )
	{
	    buffer[ bufferp++ ] = c;
	}
    }
    while ( bufferp == 0 ||  (c != 0x0a)/*( c != 0x0a && c != 0x0d )*/ );

    buffer[bufferp] = 0;

    return buffer;
}

char * mdm_get_line_SMS _P1( (fd), int fd )
{
    static char buffer[200];
    int bufferp;
    char c;

    bufferp = 0;
    lprintf( L_JUNK, "got:" );

    do
    {
	if( mdm_read_byte( fd, &c ) != 1 )
	{
	    lprintf( L_ERROR, "mdm_get_line: cannot read byte, return" );
	    return NULL;
	}

	lputc( L_JUNK, c );

	if ( isprint( c ) &&
	     bufferp < sizeof(buffer) )
	{
	    buffer[ bufferp++ ] = c;
	}
    }
    while ( bufferp == 0 || ((c != 0x0A)  && (c != 0x20))/*( c != 0x0a && c != 0x0d )*/ );

    buffer[bufferp] = 0;

    return buffer;
}

/* wait for a given modem response string,
 * handle all the various class 2 / class 2.0 status responses
 */

static boolean fwf_timeout = FALSE;

static RETSIGTYPE fwf_sig_alarm(SIG_HDLR_ARGS)      	/* SIGALRM handler */
{
    signal( SIGALRM, fwf_sig_alarm );
    lprintf( L_WARN, "Warning: got alarm signal!" );
    fwf_timeout = TRUE;
}

/* send a command string to the modem, terminated with the
 * MODEM_CMD_SUFFIX character / string from policy.h
 */

int mdm_send _P2( (send, fd),
		  char * send, int fd )
{
#ifdef DO_CHAT_SEND_DELAY
    delay(DO_CHAT_SEND_DELAY);
#endif

    lprintf( L_MESG, "mdm_send: '%s'", send );

    if ( write( fd, send, strlen( send ) ) != strlen( send ) ||
	 write( fd, MODEM_CMD_SUFFIX, sizeof(MODEM_CMD_SUFFIX)-1 ) !=
	        ( sizeof(MODEM_CMD_SUFFIX)-1 ) )
    {
	lprintf( L_ERROR, "mdm_send: cannot write" );
	return ERROR;
    }

    return NOERROR;
}

/* send a char to the modem, terminated with the
 * MODEM_CMD_SUFFIX character / string from policy.h
 */

int mdm_send_byte _P2( (send, fd),
		  char * send, int fd )
{
    lprintf( L_MESG, "mdm_send_char: '%s'", send );

    if ( write( fd, send, 1 ) != 1)
    {
	lprintf( L_ERROR, "mdm_send: cannot write" );
	return ERROR;
    }

    return NOERROR;
}
/*
int mdm_Send_SMS _P3( (Number , Text , fd),
			char * Number, char *Text , int fd)
{char AT[300]="AT+CMGS=";
 char CMGF[50]="AT^SM20=1,0+CMGF=0+CSCS=GSM\x0D"; //UCS2 / GSM
 char * temp;
 int c=23;
 char t[20];

 //char PDU[50]="\x00\x05\x00\x0C\x91\x94\x71\x22\x36\x23\x97\x00\x00\x0B\xD4\xF2\x9C\x4E\x2F\xE3\xE9\xBA\x4D\x19\x00";
 //char PDU[50]="\x00\x05\x00\x0C\x81\x10\x27\x62\x33\x72\xF9\x00\x00\x0B\xD4\xF2\x9C\x4E\x2F\xE3\xE9\xBA\x4D\x19\x00";

 //7bit
 //AT+CMGW=25 //33
 //char PDU[50]="\x07\x91\x94\x71\x22\x72\x30\x33\x11\x00\x0C\x91\x94\x71\x22\x36\x23\x97\x00\x00\xAA\x0C\x80\x83\x01\x00\x30\x14\x00\x85\x83\x01\x00";

 //8bit
 //AT+CMGW=26 //34
 //char PDU[50]="\x07\x91\x94\x71\x22\x72\x30\x33\x24\x00\x0C\x91\x94\x71\x22\x36\x23\x97\x00\x04\xAA\x0C\x48\x6F\x77\x20\x61\x72\x65\x20\x79\x6F\x75\x3F";

 char PDU[50]="\x07\x91\x94\x71\x22\x72\x30\x33\x24\x0c\x91\x94\x71\x22\x36\x23\x97\x00\x00\x50\x21\x22\x41\x70\x20\x40\x01\x41";
  //char PDU[50]="\x00\x01\x00\x0A\x91\x94\x71\x22\x36\x23\x97\x00\x00\x05\xC8\x30\x9B\xFD\x06"; //19

 //char PDU[50]="\x07\x91\x94\x71\x22\x72\x30\x33\x11\x00\x0C\x91\x94\x71\x22\x36\x23\x00\x00\x04\xAA\x0C\x48\x6F\x77\x20\x61\x72\x65\x20\x79\x6F\x75\x3F";

 //16bit
 //AT+CMGW=38
 //char PDU[50]="\x00\x11\x00\x0B\x92\x10\x27\x62\x33\x72\xF9\x00\x08\xAA\x18\x00\x48\x00\x6F\x00\x77\x00\x20\x00\x61\x00\x72\x00\x65\x00\x20\x00\x79\x00\x6F\x00\x75\x00\x3F";
//AT+CMGW=24
//07 91 1356131313F3 11 00 0A 92 6021436587 00 00 AA0C8083010030140085830100


 //01 72 26 33 27 9F
 //\x10\x27\x62\x33\x72\F9
 //char PDU[50]="\x00\x05\x00\x0B\x81\x10\x27\x62\x33\x72\xF9\x00\x00\x0B\xD4\xF2\x9C\x4E\x2F\xE3\xE9\xBA\x4D\x19\x00";
 enum SMS_State
 {SMS_INIT=1,
  WAIT_FOR_INIT_ECHO,
  WAIT_FOR_INIT_OK,
  SMS_CMGS_TEL,
  WAIT_FOR_CMGS_TEL,
  WAIT_FOR_ENTER_TEXT,
  ENTER_TEXT,
  SMS_END
 };

    char * l; int i,n=-1;
    static char rbuf[80];


    // wait for OK or ERROR, *without* side effects (as fax_wait_for
    // would have)
    //
    signal( SIGALRM, fwf_sig_alarm ); alarm(20); fwf_timeout = FALSE;

    i=0;
    rbuf[0] = '\0';

    int State=SMS_INIT;//WAIT_FOR_CMGS_TEL;
    while(1)
    {switch(State)
      {case SMS_INIT:		lprintf( L_MESG, "mdm_Send_SMS: SMS_INIT! (%d)",c );
				//SEND
				if ( write( fd, CMGF, strlen( CMGF ) ) != strlen( CMGF ))
    				 {lprintf( L_ERROR, "mdm_Send_SMS: CMGF cannot write" );
      				  return ERROR;
    				 }
				else
				 State=WAIT_FOR_INIT_ECHO;
				break;
	case WAIT_FOR_INIT_ECHO:lprintf( L_MESG, "mdm_Send_SMS: WAIT_FOR_INIT_ECHO!" );
				temp = mdm_get_line( fd );
				if(strncmp(temp,"AT^SM20=1,0",11) == 0)
				 {State=WAIT_FOR_INIT_OK;
				 }
				else
				 {lprintf( L_MESG, "mdm_Send_SMS: ERROR AT+CMGF" );
				  return ERROR;
				 }
				break;
	case WAIT_FOR_INIT_OK:	lprintf( L_MESG, "mdm_Send_SMS: WAIT_FOR_INIT_OK!" );
				temp = mdm_get_line( fd );
				if(strncmp(temp,"OK",2) == 0)
				 {State=SMS_CMGS_TEL;
				 }
				else
				 {lprintf( L_MESG, "mdm_Send_SMS: ERROR AT+CMGF" );
				  return ERROR;
				 }
				break;
	case SMS_CMGS_TEL:	lprintf( L_MESG, "mdm_Send_SMS: SMS_CMGS_LEN!" );
				strcpy(AT,"AT+CMGS=");
				sprintf(t,"%d",c);
				strcat(AT,t);
				lprintf( L_MESG, "mdm_Send_SMS: %s", AT );
				if ( write( fd, AT, strlen( AT ) ) != strlen( AT ))
				 {lprintf( L_ERROR, "mdm_Send_SMS: cannot write" );
				  return ERROR;
				 }
				if(mdm_send_byte("\x0D",fd) == ERROR)
				 {lprintf( L_ERROR, "mdm_Send_SMS: mdm_send_byte(x0D,fd) -> error" );
				  return ERROR;
				 }
				State=WAIT_FOR_CMGS_TEL;
				break;
	case WAIT_FOR_CMGS_TEL:	lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_CMGS_TEL!" );
      				l = mdm_get_line_SMS( fd );
				lprintf( L_MESG, "mdm_Send_SMS: -> %s",l );
				if(strncmp(l,"AT+CMGS=",8)==0)
				 {lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_CMGS_TEL! -> OK!" );
				  State=WAIT_FOR_ENTER_TEXT;
				 }
				break;
       	case WAIT_FOR_ENTER_TEXT:
       				lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_ENTER_TEXT!" );
       				l = mdm_get_line_SMS( fd );
				if(strncmp(l,">",1)==0)
				 State=ENTER_TEXT;
				break;
	case ENTER_TEXT:	lprintf( L_MESG, "mdm_Send_SMS: ENTER_TEXT!" );
				alarm(0); signal( SIGALRM, SIG_DFL );
				//if ( write( fd, PDU , strlen( Text ) ) != strlen( Text ))
				if ( write( fd, PDU , 19 ) != 19)
    				 {lprintf( L_ERROR, "mdm_Send_SMS: cannot write" );
      		 		  return ERROR;
    				 }
	       			else
	        		 {if(mdm_send_byte("\x1A",fd) == ERROR)
   		  		   return ERROR;
		 		  signal( SIGALRM, fwf_sig_alarm ); alarm(10); fwf_timeout = FALSE;
		  		  i=0;
    		  		  rbuf[0] = '\0';
		 		  while(1)
		  		   {l = mdm_get_line( fd );

		   		    if ( l == NULL )
				     {State=SMS_END;
				      break;				// error
				     }
				    if ( strcmp( l, PDU ) == 0 )
				     continue;		// command echo

				    if ( strcmp( l, "OK" ) == 0 ||			// final string
		 		     strcmp( l, "ERROR" ) == 0 )
				     {break;
				     }
				    i++;
				    lprintf( L_NOISE, "mdm_gis: string %d: '%s'", i, l );
				    int SMS_SEND_STATE=-1;
				    if(strncmp(l,"+CMGS:",6) == 0)
				     {lprintf( L_NOISE, "-> SMS Gesendet!", i, l );
				      SMS_SEND_STATE=1;
				     }
				    if(strncmp(l,"+CMS ERROR:",5) == 0)
				     {SMS_SEND_STATE=1;
				      lprintf( L_NOISE, "-> SMS nicht Gesendet!", i, l );
				     }
				   }
				 }

				State=SMS_END;
				//State=SMS_INIT;c++;
				break;
	case SMS_END:		alarm(0); signal( SIGALRM, SIG_DFL );
				//if ( l == NULL ) return ERROR;			// error
				return NOERROR;
				break;
      }
    }
}
*/

void CheckSmsString(char *String)
 {int i;
  char c;
  char C[]="abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789*:.,<>-#()=;";

  for(i=0; i<strlen(String) ; i++)
   {c=*(String+i);
    if(strcspn(&c,C)!=0)
     *(String+i)='?';
   }
 }

int mdm_Send_SMS _P3( (Number , Text , fd),
			char * Number, char *Text , int fd)
{char AT[300]="AT+CMGS=";
 char CMGF[20]="AT+CMGF=1^SM20=1,0\x0D";
 char * temp;

 enum SMS_State
 {SMS_INIT=1,
  WAIT_FOR_INIT_ECHO,
  WAIT_FOR_INIT_OK,
  SMS_CMGS_TEL,
  WAIT_FOR_CMGS_TEL,
  WAIT_FOR_ENTER_TEXT,
  ENTER_TEXT,
  SMS_END,
  SMS_ERROR, 
 };

    char * l; int i;//,n=-1;
    static char rbuf[80];

    //CheckSmsString(Text);
    lprintf( L_MESG, "mdm_Send_SMS: CheckString: %s",Text );
    //return (1);
    // wait for OK or ERROR, *without* side effects (as fax_wait_for
    // would have)
    //


    i=0;
    rbuf[0] = '\0';

    int State=SMS_INIT;//WAIT_FOR_CMGS_TEL;
    while(1)
    {signal( SIGALRM, fwf_sig_alarm ); alarm(20); fwf_timeout = FALSE;
     switch(State)
      {case SMS_INIT:		lprintf( L_MESG, "mdm_Send_SMS: SMS_INIT!" );
				mdm_lock_tty();
				//SEND
				if ( write( fd, CMGF, strlen( CMGF ) ) != strlen( CMGF ))
    				 {lprintf( L_ERROR, "mdm_Send_SMS: CMGF cannot write" );
      				  State=SMS_ERROR;
				  break;
    				 }
				else
				 State=WAIT_FOR_INIT_ECHO;
				break;
	case WAIT_FOR_INIT_ECHO:lprintf( L_MESG, "mdm_Send_SMS: WAIT_FOR_INIT_ECHO!" );
				temp = mdm_get_line( fd );
				if(strncmp(temp,"AT+CMGF=1",9) == 0)
				 {State=WAIT_FOR_INIT_OK;
				 }
				else
				 {lprintf( L_MESG, "mdm_Send_SMS: ERROR AT+CMGF" );
				  State=SMS_ERROR;
				 }
				break;
	case WAIT_FOR_INIT_OK:	lprintf( L_MESG, "mdm_Send_SMS: WAIT_FOR_INIT_OK!" );
				temp = mdm_get_line( fd );
				if(strncmp(temp,"OK",2) == 0)
				 {mdm_release_tty();
				  State=SMS_CMGS_TEL;
				 }
				else
				 {lprintf( L_MESG, "mdm_Send_SMS: ERROR AT+CMGF" );
				  State=SMS_ERROR;
				 }
				break;
	case SMS_CMGS_TEL:	sleep(2);
				lprintf( L_MESG, "mdm_Send_SMS: SMS_CMGS_TEL!" );
				strcat(AT,Number);
				lprintf( L_MESG, "mdm_Send_SMS: [%s]", AT );
				mdm_lock_tty();
				if ( write( fd, AT, strlen( AT ) ) != strlen( AT ))
				 {lprintf( L_ERROR, "mdm_Send_SMS: cannot write" );
				  State=SMS_ERROR;
				  break;
				 }
				if(mdm_send_byte("\x0D",fd) == ERROR)
				 {lprintf( L_ERROR, "mdm_Send_SMS: mdm_send_byte(x0D,fd) -> error" );
				  State=SMS_ERROR;
				  break;
				 }
				State=WAIT_FOR_CMGS_TEL;
				break;
	case WAIT_FOR_CMGS_TEL:	lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_CMGS_TEL!" );
      				l = mdm_get_line_SMS( fd );
				lprintf( L_MESG, "mdm_Send_SMS: -> %s",l );
				if(l == NULL)
				 {lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_CMGS_TEL! -> ERROR!" );
				  State=SMS_ERROR;
				  break;
				 }
				if(strncmp(l,"AT+CMGS=",8)==0)
				 {lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_CMGS_TEL! -> OK!" );
				  State=WAIT_FOR_ENTER_TEXT;
				 }
				else
				 {lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_CMGS_TEL! -> ERROR!" );
				  State=SMS_ERROR;
				 }
				break;
       	case WAIT_FOR_ENTER_TEXT:
       				lprintf( L_MESG, "mdm_Send_SMS: Waiting for WAIT_FOR_ENTER_TEXT!" );
       				l = mdm_get_line_SMS( fd );
				if(strncmp(l,">",1)==0)
				 {mdm_release_tty();
				  State=ENTER_TEXT;
				 }
				else
				 {lprintf( L_MESG, "mdm_Send_SMS: WWaiting for WAIT_FOR_ENTER_TEXT! -> ERROR!" );
				  State=SMS_ERROR;
				 }
				break;
	case ENTER_TEXT:	lprintf( L_MESG, "mdm_Send_SMS: ENTER_TEXT!" );
				mdm_lock_tty();
				alarm(0); signal( SIGALRM, SIG_DFL );
	       			State=SMS_ERROR;
				if ( write( fd, Text , strlen( Text ) ) != strlen( Text ))
    				 {lprintf( L_ERROR, "mdm_Send_SMS: cannot write" );
      		 		  //return ERROR;
				  break;
				 }
	       			else
	        		 {if(mdm_send_byte("\x1A",fd) == ERROR)
				   {
   		  		    break;
				   //return ERROR;
		 		   }

				  signal( SIGALRM, fwf_sig_alarm ); alarm(10); fwf_timeout = FALSE;
		  		  i=0;
    		  		  rbuf[0] = '\0';
		 		  while(1)
		  		   {l = mdm_get_line( fd );

		   		    if ( l == NULL )
				     {//State=SMS_END;
				      break;				// error
				     }
				    if ( strcmp( l, Text ) == 0 )
				     continue;		// command echo

				    //if ( strcmp( l, "OK" ) == 0 ||			// final string
		 		    // strcmp( l, "ERROR" ) == 0 ) break;

				    i++;
				    lprintf( L_NOISE, "mdm_gis: string %d: '%s'", i, l );
				    int SMS_SEND_STATE=-1;
				    if(strncmp(l,"+CMGS:",6) == 0)
				     {lprintf( L_NOISE, "-> SMS Gesendet! : %s",l);
				      SMS_SEND_STATE=1;
				      State=SMS_END;
				     }
				    if(strncmp(l,"+CMS ERROR:",5) == 0)
				     {SMS_SEND_STATE=1;
				      lprintf( L_NOISE, "-> SMS nicht Gesendet! : %s",l);
				     }
				    if ( strstr(l,"OK") != NULL) /*strcmp( l, "OK" ) == 0)*/
				     {State=SMS_END;
				      lprintf( L_NOISE, "-> !!! OK !!!");
				      break;
				     }
				    if ( strstr(l,"ERROR") != NULL) /*strcmp( l, "ERROR" ) == 0)*/
				     {lprintf( L_NOISE, "-> !!! Error !!!");
				      break;
				     }
				   }
				 }

				//State=SMS_END;
				break;
	case SMS_END:		alarm(0); signal( SIGALRM, SIG_DFL );
				mdm_release_tty();
				//if ( l == NULL ) return ERROR;			// error
				lprintf( L_NOISE, "-> SMS_END");
				return NOERROR;
				break;
        case SMS_ERROR:		alarm(0); signal( SIGALRM, SIG_DFL );
				mdm_release_tty();
				lprintf( L_NOISE, "-> SMS_ERROR");
				return ERROR;
				break;
      }
    }
}

/* simple send / expect sequence, for things that do not require
 * parsing of the modem responses, or where the side-effects are
 * unwanted.
 */

int mdm_command _P2( (send, fd), char * send, int fd )
{
    char * l;

    mdm_lock_tty();

    if ( mdm_send( send, fd ) == ERROR ) return ERROR;

    /* wait for OK or ERROR, *without* side effects (as fax_wait_for
     * would have)
     */
    signal( SIGALRM, fwf_sig_alarm ); alarm(10); fwf_timeout = FALSE;

    do
    {
	l = mdm_get_line( fd );
	if ( l == NULL ) break;
	lprintf( L_NOISE, "mdm_command: string '%s'", l );
    }
    while ( strcmp( l, "OK" ) != 0 && strcmp( l, "ERROR" ) != 0 );

    alarm(0); signal( SIGALRM, SIG_DFL );

    mdm_release_tty();

    if ( l == NULL || strcmp( l, "ERROR" ) == 0 )
    {
	lputs( L_MESG, " -> ERROR" );
	return ERROR;
    }
    lputs( L_MESG, " -> OK" );
	
    return NOERROR;
}

/* mdm_read_byte
 * read one byte from "fd", with buffering
 * caveat: only one fd allowed (only one buffer), no way to flush buffers
 */

int mdm_read_byte _P2( (fd, c),
		       int fd, char * c )
{
static char frb_buf[512];
static int  frb_rp = 0;
static int  frb_len = 0;

    if ( frb_rp >= frb_len )
    {
	frb_len = read( fd, frb_buf, sizeof( frb_buf ) );
	if ( frb_len <= 0 )
	{
	    if ( frb_len == 0 ) errno = 0;	/* undefined otherwise */
	    lprintf( L_ERROR, "mdm_read_byte: read returned %d", frb_len );
	    return frb_len;
	}
	frb_rp = 0;
    }

    *c = frb_buf[ frb_rp++ ];
    return 1;
}

/* for modem identify (and maybe other nice purposes, who knows)
 * this function is handy:
 * - send some AT command, wait for OK/ERROR or 10 seconds timeout
 * - return a pointer to a static buffer holding the "nth" non-empty
 *   answer line from the modem (for multi-line responses), or the 
 *   last line if n==-1
 */
char * mdm_get_idstring _P3( (send, n, fd), char * send, int n, int fd )
{
    char * l; int i;
    static char rbuf[80];

    mdm_lock_tty();

    if ( mdm_send( send, fd ) == ERROR ) return "<ERROR>";

    /* wait for OK or ERROR, *without* side effects (as fax_wait_for
     * would have)
     */
    signal( SIGALRM, fwf_sig_alarm ); alarm(10); fwf_timeout = FALSE;

    i=0;
    rbuf[0] = '\0';

    while(1)
    {
	l = mdm_get_line( fd );

	if ( l == NULL ) break;				/* error */
	if ( strcmp( l, send ) == 0 ) continue;		/* command echo */

        if ( strcmp( l, "OK" ) == 0 ||			/* final string */
	     strcmp( l, "ERROR" ) == 0 ) break;

        i++;
	lprintf( L_NOISE, "mdm_gis: string %d: '%s'", i, l );

	if ( i==-1 || i==n )		/* copy string */
	    { strncpy( rbuf, l, sizeof(rbuf)-1); rbuf[sizeof(rbuf)-1]='\0'; }
    }

    alarm(0); signal( SIGALRM, SIG_DFL );

    mdm_release_tty();

    if ( l == NULL ) return "<ERROR>";			/* error */

    return rbuf;
}


int mdm_get_string _P4( (send, n, fd, receive), char * send, int n, int fd, char **receive )
{
    char * l; int i,ret = -1;
    static char rbuf[80];

    mdm_lock_tty();

    if ( mdm_send( send, fd ) == ERROR ) return -2;	//Error: Send

    /* wait for OK or ERROR, *without* side effects (as fax_wait_for
     * would have)
     */
    signal( SIGALRM, fwf_sig_alarm ); alarm(10); fwf_timeout = FALSE;

    i=0;
    rbuf[0] = '\0';

    while(1)
    {
	l = mdm_get_line( fd );

	if ( l == NULL ) break;				/* error */
	if ( strcmp( l, send ) == 0 ) continue;		/* command echo */

	if ( strcmp( l, "OK" ) == 0 ) /* final string */
	 {ret = 1;
	  break;
         }		
	if( strcmp( l, "ERROR" ) == 0 )
	 {ret = 0;
	  break;
	 }

        i++;
	lprintf( L_NOISE, "mdm_gis: string %d: '%s'", i, l );

	if ( i==-1 || i==n )		/* copy string */
	    { strncpy( rbuf, l, sizeof(rbuf)-1); rbuf[sizeof(rbuf)-1]='\0'; }
    }

    alarm(0); signal( SIGALRM, SIG_DFL );

    mdm_release_tty();

    if ( l == NULL ) return -1;	//Error: Timeout

    *receive = rbuf;
    return ret;	//Modem Answer: 1=OK, 0=Errot 
}
