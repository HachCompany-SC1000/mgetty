# Makefile for the "tools" subtree
# of mgetty+sendfax
#
# RCS: $Id: Makefile,v 1.6 1999/01/04 21:47:36 gert Exp $
#

CC=gcc
CFLAGS=-O2 -I..

SCRIPTS=kvg cutbl
PROGRAMS=ltest mid
TOOLS=

all: $(SCRIPTS) $(PROGRAMS)

kvg: kvg.in ../sedscript
	../sedscript <kvg.in >kvg

cutbl: cutbl.in ../sedscript
	../sedscript <cutbl.in >cutbl

../sedscript: ../policy.h ../Makefile ../mksed.c
	@cd .. ; $(MAKE) sedscript

ltest: ltest.o ../tio.o ../io.o
	$(CC) $(CFLAGS) -o ltest $(LDFLAGS) ltest.o ../tio.o ../io.o $(LIBS)

ltest.o: ltest.c ../tio.h ../mgetty.h
	$(CC) $(CFLAGS) -c -o ltest.o ltest.c

mid: mid.o ../tio.o ../io.o ../locks.o
	$(CC) $(CFLAGS) -o mid $(LDFLAGS) mid.o ../tio.o \
		../io.o ../locks.o $(LIBS)

mid.o: mid.c ../tio.h ../mgetty.h
	$(CC) $(CFLAGS) -c -o mid.o mid.c

vhangup: vhangup.c
	$(CC) $(CFLAGS) -o vhangup vhangup.c

clean:
	-rm -f $(SCRIPTS) $(PROGRAMS) *.o

fullclean:
	-rm -f $(SCRIPTS) $(PROGRAMS)

install: all
#
# programs / scripts
#
	-for i in $(SCRIPTS); do \
		mv -f $(BINDIR)/$$i $(BINDIR)/$$i.old 2>/dev/null ; \
		$(INSTALL) -m 755 $$i $(BINDIR) ; \
	done
