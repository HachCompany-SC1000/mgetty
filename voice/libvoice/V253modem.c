/*
 * V253modem.c
 *
 * This file contains the commands for V253 complaient modems
 *
  For this File I (Juergen.Kosel@gmx.de) used the ELSA.c as template and replaced all pre-V.253 commands
  with the commands defined in the ITU V.253 specification.
  Newer ELSA-modems follow this specification.
  So some ELSA-modems like the "ML 56k pro" could be used with the ELSA.c (and the old commands)
  AND with this V253modem (But for these modems you should set sample rate in your voice.conf to 7200
  otherwise fax/data-callingtonedetection will fail).
  The "ML 56k FUN" and future ELSA-modems work only with this V253modem.

  Because there are only V.253 commands here, this IS a GENERIC-DRIVER!

  Hint: Recorded voice files are in .ub format (refer to the sox manpage about this) except the header.
        So you can use this files with sox.
 *
 * $Id: V253modem.c,v 1.6 2002/11/25 21:14:51 gert Exp $
 *
 */


#include "../include/V253modem.h"

     int V253modem_init (void)
     {
     char buffer[VOICE_BUF_LEN];

     reset_watchdog();
     voice_modem_state = INITIALIZING;
     lprintf(L_MESG, "initializing V253 voice modem");


     /* enabling voicemode */
     sprintf(buffer, "AT+FCLASS=8");
     if (voice_command(buffer, "OK") != VMA_USER_1)
          lprintf(L_WARN, "could not set +FCLASS=8");


     /* set silence-sensitvity-level and silence length */
/*
first value of the +vsd command means:
 0: silence detection of/ or silencecompression with the +vsm command
128: manufacturer default
<128: less aggressive [more sensitive, lower noise levels considered to
be silence].
>128: more aggressive [less sensitive, higher noise levels considered to be silence].

 */
#if 0 /* enable this when cvd.rec_silence_threshold.d.i  is set as an absolut value
(with default 128) instead of percent */
     sprintf(buffer, "AT+VSD=%d,%d", cvd.rec_silence_threshold.d.i , cvd.rec_silence_len.d.i);
#else /* until this, the sensitvity is harcoded with manufaturer default! */
    sprintf(buffer, "AT+VSD=%d,%d", 128 , cvd.rec_silence_len.d.i);
#endif
     if (voice_command(buffer, "OK") != VMA_USER_1)
          lprintf(L_WARN, "can't set silence threshold VSD");

/* set transmit-gain manufacturer default is 128 so the vgetty default of 127 isn't far away */
     if (cvd.transmit_gain.d.i == -1)
          cvd.transmit_gain.d.i = 50;

     sprintf(buffer, "AT+VGT=%d", cvd.transmit_gain.d.i * 255 / 100 );

     if (voice_command(buffer, "OK") != VMA_USER_1)
          lprintf(L_WARN, "can't set speaker volume");

/* set recieve-gain manufacturer default is 128 so the vgetty default of 127 isn't far away */

     if (cvd.receive_gain.d.i == -1)
          cvd.receive_gain.d.i = 50;

     sprintf(buffer, "AT+VGR=%d", cvd.receive_gain.d.i * 255 / 100 );

     if (voice_command(buffer, "OK") != VMA_USER_1)
          lprintf(L_WARN, "can't set record volume");

     /* Delay after ringback or before any ringback
      * before modem assumes phone has been answered.
      */
     sprintf(buffer,
             "AT+VRA=%d;+VRN=%d",
             cvd.ringback_goes_away.d.i,     /* 1/10 seconds */
             cvd.ringback_never_came.d.i/10); /* seconds */

     if (voice_command(buffer, "OK") != VMA_USER_1)
          lprintf(L_WARN, "setting ringback delay didn't work");


/* set hardflow */
     if ((cvd.do_hard_flow.d.i) && (voice_command("AT+IFC=2,2", "OK") ==
      VMA_USER_1) )
          {
          TIO tio;
          tio_get(voice_fd, &tio);
          tio_set_flow_control(voice_fd, &tio, FLOW_HARD);
          tio_set(voice_fd, &tio);
          }
     else
          lprintf(L_WARN, "can't turn on hardware flow control");


     /* enable callerid (if supported) and setformat */
     voice_command("AT+VCID=1", "OK");
     /* mgetty supports formated callerid output */

     /* set hangupcontroll */
     voice_command("AT+VNH=0", "OK");
     /* this means modem will hangup after is has switched to data/faxmode as normal
        with +VNH=1 the modem doesn't make an automatic disconnect if connect fails in
	data/faxmode so mgetty could switch back to voicemode */

     /* voice-inactivity-timer: This means that the modem should go back
        to fclass 0 and enable autobauding (which wasn't disabled by vgetty with
        AT+IPR or AT+VPR). Since it's in the TODO list that vgetty stays in voice
        the voice-inactivity-timer is disabled.
     */
     voice_command("AT+VIT=0", "OK");

     /* enable distinctivering in pulse/pauseformat
       this will look like this:
DRON=20
RING
DROF=40
DRON=20
RING */
     sprintf(buffer, "AT+VDR=1,%d", cvd.ring_report_delay.d.i);
     voice_command(buffer, "OK");

     voice_modem_state = IDLE;
     return(OK);
     }
/*
Table 17/V.253 - Compression method identifier numerics and strings
# 	String ID 	Description
------------------------------------------------
0 	SIGNED PCM 	Linear PCM sampling using two complement signed numbers
1 	UNSIGNED PCM 	Linear PCM sampling using unsigned numbers
2 	RESERVED
3 	G.729.A 	G.729 Annex A - Recommendation V.70 DSVD default coder.
4 	G.711U 		u-Law companded PCM
5 	G.711A 		A-Law companded PCM
6 	G.723 		H.324 Low bit rate videotelephone default speech coder
7 	G.726 		ITU-T 40, 32, 24, 16 kit/s ADPCM.
8 	G.728 		H.320 Low bit rate videotelephone speech coder
9-127 			Reserved for future standardization
128-255 		Manufacturer specific
*/

     int V253modem_set_compression (int *compression, int *speed, int *bits)
     {
     char buffer[VOICE_BUF_LEN];
     int Kompressionmethod = 1; /* a littlebit germinglish :-) */
     int sil_sense = 0; 	/* silence compression sensitivity */
     int sil_clip = 0;  	/* silence clip                    */
     reset_watchdog();

     switch (*compression)
     {
       case 0:
       case 1:
       case 8:
       {
          Kompressionmethod = 1;
          *bits=8;
          break;
       }
     /*  default is 8 Bit PCM/8 bit linear which should be supported by most modems
         and be mapped to the V.253 defined AT+VSM=1 .
         With 8000 samples/sec it's also the default for soundcards.

         On the otherside the compressionmodes from the Elsa.c
         are mapped to the manufacturer specific +VSM values (above 127)
         so voice files recorded with the &Elsa driver can be played with
         this &V253modem driver (and the same modem of course) */
       case 2:       /*  2bit ADPCM for some ELSA-modems */
       {
         *bits = 2;
         if (voice_command("AT+VSM=140,7200", "OK")!= VMA_USER_1)
         {
         /* there are two diffrent implementations trying one first,
            if this fails we try the other one later */
           Kompressionmethod = 129;
         }
         else
         {
           Kompressionmethod = 140;
         }
         *speed=7200;
         break;
        }
       case 4:           /* 4bit ADPCM for some ELSA-modems */
       {
         if (voice_command("AT+VSM=141,7200", "OK")!= VMA_USER_1)
         {
         /* there are two diffrent implementations trying one first,
            if this fails we try the other one later */

           Kompressionmethod = 131;
         }
         else
         {
           Kompressionmethod = 141;
         }
         *bits=4;
         *speed=7200;
         break;
       }
       case 5:
       {
         Kompressionmethod = 129;
         *bits = 4;      /* 129 ->4bit ADPCM for the ML 56k Fun*/
         break;
       }
       case 6:
       {
         Kompressionmethod = 131;
         *bits = 8;      /* 8bit uLAW for the ML 56k Fun*/
         *speed=8000;
         break;
       }
       case 7:
       {
         Kompressionmethod = 132;
         *bits = 8;      /* 8bit aLAW for the ML 56k Fun*/
         *speed=8000;
         break;
       }
       case 9:        /* ITU defined unsigned PCM */
       {
          Kompressionmethod = 0;
          *bits=8;
          break;
       }
       case 10:        /* ITU defined uLaw */
       {
          Kompressionmethod = 4;
          *bits=8;
          break;
       }
       case 11:        /* ITU defined aLaw */
       {
          Kompressionmethod = 5;
          *bits=8;
          break;
       }
       case 12:        /* ITU defined signed PCM 16-bit Intel Order */
       {
	 			/* Chipset Agere/Lucent Venus v.92 found on 
	  			 * ActionTec v.92 Call Waiting PCI modem    */
	 /* *speed=8000;	 * Default RATE is 8000 */
	 Kompressionmethod = 0; /* Signed PCM                 	   	    */
     /*  sil_sense = 0;  	* silence compression sensitivity Default=0 */
     /*  sil_clip = 0;  	* silence clip    Default=0                 */
	 *bits=16;		/* 16 bit			   	    */
	 break;
       }


       default:
       {
          lprintf(L_WARN, "compression method %d is not supported -> edit voice.conf",*compression);
        /*  return(FAIL);  */
          Kompressionmethod = 1;
          *bits=8;
       }
     }
     if (*speed == 0)
     /* default value for the PCM voiceformat is 8000 */
          *speed = 8000;

     sprintf(buffer, "AT+VSM=%d,%d",Kompressionmethod, *speed);
     /* only no compression is common! */
     /* ATTENTION the AT+VSM=? output is diffrent from the AT+VSM=<Parameter> */
     if (voice_command(buffer, "OK")!= VMA_USER_1)
     sprintf(buffer, "AT+VSM=%d,%d,%d,%d",Kompressionmethod, *speed, sil_sense, sil_clip); 
     if (voice_command(buffer, "OK")!= VMA_USER_1)
      {
         lprintf(L_WARN, "can't set compression and speed");
         /*return(FAIL);*/   /* when we don't say FAIL here,
           the modem can still record the message with its
           last/default compression-setting */
         voice_command("AT+VSM?", "OK");
         /* write the actual setting to the logfile */
      }

     lprintf(L_NOISE, "Just for info: port_speed should be greater than %d bps",(*bits)*(*speed)*10/8);
     /* 8 Databits + 1 Stopbit +1 startbit  */
     return(OK) ;
     }

     int V253modem_set_device (int device)
     {
       int Result;
       reset_watchdog();
       lprintf(L_JUNK, "%s: %s: (%d)", voice_modem_name, 
	       voice_device_mode_name(device), device);

     switch (device)
          {
          case NO_DEVICE:
	    Result = voice_command("AT+VLS=0", "OK");
	    break;
          case DIALUP_LINE:
	    Result = voice_command("AT+VLS=1", "OK");
	    break;
          case EXTERNAL_MICROPHONE:
	    Result = voice_command("AT+VLS=11", "OK");
	    break;
          case INTERNAL_MICROPHONE:
	    Result = voice_command("AT+VLS=6", "OK");
	    break;
          case INTERNAL_SPEAKER:
	    Result = voice_command("AT+VLS=4", "OK");
	    break;
          case EXTERNAL_SPEAKER:
	    Result = voice_command("AT+VLS=8", "OK");
	    break;
          case LOCAL_HANDSET :
	    Result = voice_command("AT+VLS=2", "OK");
	    break;
          case DIALUP_WITH_EXT_SPEAKER :
	    Result = voice_command("AT+VLS=9", "OK");
	    break;
          case DIALUP_WITH_INT_SPEAKER :
	    Result = voice_command("AT+VLS=5", "OK");
	    break;
          case DIALUP_WITH_LOCAL_HANDSET :
	    Result = voice_command("AT+VLS=3", "OK");
	    break;
          case DIALUP_WITH_EXTERNAL_MIC_AND_SPEAKER:
	    Result = voice_command("AT+VLS=13", "OK");
	    break;
          case DIALUP_WITH_INTERNAL_MIC_AND_SPEAKER:
	    Result = voice_command("AT+VLS=7", "OK");
	    break;
	  default:
	    lprintf(L_WARN, "%s: Unknown device (%d)", 
		    voice_modem_name, device);
	    return(FAIL);
          }

     if (Result != VMA_USER_1)   
       {
	 lprintf(L_WARN, "can't set %s (modem hardware can't do that)",
		 voice_device_mode_name(device));
	 return(VMA_DEVICE_NOT_AVAIL);
       }
     return(OK);
     }

/* Only verifies the RMD name */
#define V253modem_RMD_NAME "V253modem"
#define ELSA_RMD_NAME "ELSA"
int V253_check_rmd_adequation(char *rmd_name) 
{
   return !strncmp(rmd_name,
                   V253modem_RMD_NAME,
                   sizeof(V253modem_RMD_NAME))
          || !strncmp(rmd_name,
                      ELSA_RMD_NAME,
                      sizeof(ELSA_RMD_NAME));
}


const char V253modem_pick_phone_cmnd[] = "AT+FCLASS=8";  /* because this will be followed by a
                                                             V253modem_set_device (DIALUP_LINE)
                                                             -> this picks up the line!*/
const char V253modem_pick_phone_answr[] = "VCON|OK";


const char V253modem_hardflow_cmnd[] = "AT+IFC=2,2";
const char V253modem_softflow_cmnd[] = "AT+IFC=1,1";

const char V253modem_beep_cmnd[] = "AT+VTS=[%d,,%d]";


voice_modem_struct V253modem =
    {
    "V253 modem",
    V253modem_RMD_NAME,
     (char *) V253modem_pick_phone_cmnd,
     (char *) V253modem_pick_phone_answr,
     (char *) V253modem_beep_cmnd,
     (char *) IS_101_beep_answr,
              IS_101_beep_timeunit,
     (char *) V253modem_hardflow_cmnd,
     (char *) IS_101_hardflow_answr,
     (char *) V253modem_softflow_cmnd,
     (char *) IS_101_softflow_answr,
     (char *) IS_101_start_play_cmnd,
     (char *) IS_101_start_play_answer,
     (char *) IS_101_reset_play_cmnd,
     (char *) IS_101_intr_play_cmnd,
     (char *) IS_101_intr_play_answr,
     (char *) IS_101_stop_play_cmnd,
     (char *) IS_101_stop_play_answr,
     (char *) IS_101_start_rec_cmnd,
     (char *) IS_101_start_rec_answr,
     (char *) IS_101_stop_rec_cmnd,
     (char *) IS_101_stop_rec_answr,
     (char *) IS_101_switch_mode_cmnd,
     (char *) IS_101_switch_mode_answr,
     (char *) IS_101_ask_mode_cmnd,
     (char *) IS_101_ask_mode_answr,
     (char *) IS_101_voice_mode_id,
     (char *) IS_101_play_dtmf_cmd,
     (char *) IS_101_play_dtmf_extra,
     (char *) IS_101_play_dtmf_answr,
    &IS_101_answer_phone,
    &IS_101_beep,
    &IS_101_dial,
    &IS_101_handle_dle,
    &V253modem_init,
    &IS_101_message_light_off,
    &IS_101_message_light_on,
    &IS_101_start_play_file,
    NULL,
    &IS_101_stop_play_file,
    &IS_101_play_file,
    &IS_101_record_file,
    &V253modem_set_compression,
    &V253modem_set_device,
    &IS_101_stop_dialing,
    &IS_101_stop_playing,
    &IS_101_stop_recording,
    &IS_101_stop_waiting,
    &IS_101_switch_to_data_fax,
    &IS_101_voice_mode_off,
    &IS_101_voice_mode_on,      /* it's also possible to say AT+FCLASS=8.0 */
    &IS_101_wait,
    &IS_101_play_dtmf,
    &V253_check_rmd_adequation,
    0
    };
