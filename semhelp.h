
#ifndef __SEMHELP_H
#define __SEMHELP_H

#define SEMMSL		250


union semun
{
  int val;			/* value for SETVAL */
  struct semid_ds *buf;		/* buffer for IPC_STAT & IPC_SET */
  unsigned short *array;	/* array for GETALL & SETALL */
  struct seminfo *__buf;	/* buffer for IPC_INFO */
  void *__pad;
};



#endif
