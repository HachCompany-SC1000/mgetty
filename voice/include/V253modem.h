#ifndef VOICE_INCLUDE_V253_H_
#define VOICE_INCLUDE_V253_H_
/*
 * Defines the functions implemented in voice/libvoice/V253modem.c
 * I wrote this with the hope that vm scripts written for a none full V253 compatible modem
 * should also run on full V253 compatible modems.
 */
#include "../include/voice.h"

int V253modem_set_device (int device);
int V253modem_init (void);
int V253modem_set_compression (int *compression, int *speed, int *bits);
int V253modem_set_device (int device);
int V253_check_rmd_adequation(char *rmd_name);
extern const char V253modem_pick_phone_cmnd[];
extern const char V253modem_pick_phone_answr[];
extern const char V253modem_hardflow_cmnd[];
extern const char V253modem_softflow_cmnd[];
extern const char V253modem_beep_cmnd[];
#endif
