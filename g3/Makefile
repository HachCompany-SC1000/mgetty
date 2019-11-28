# Makefile for the "g3 tools" subtree
# of mgetty+sendfax
#
# SCCS: $Id: Makefile,v 4.2 1997/02/02 23:45:58 gert Exp $ Copyright (c) Gert Doering
#

CC=gcc
CFLAGS=-O2 -I.. -g

G3_PROGRAMS=pbm2g3 g3cat g32pbm

all: $(G3_PROGRAMS)

pbm2g3: pbm2g3.o g3.o run_tbl.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o pbm2g3 pbm2g3.o g3.o run_tbl.o $(LIBS)

g32pbm: g32pbm.o g3.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o g32pbm g32pbm.o g3.o $(LIBS)

g3cat: g3cat.o g3.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o g3cat g3cat.o g3.o $(LIBS)

g3.o : g3.c g3.h ../ugly.h
g3cat.o : g3cat.c g3.h ../ugly.h 
g32pbm.o : g32pbm.c g3.h ../ugly.h
pbm2g3.o : pbm2g3.c g3.h ../ugly.h
run_tbl.o : run_tbl.c ../ugly.h 

clean:
	rm -f *.o

fullclean:
	rm -f *.o $(G3_PROGRAMS)

install: all
#
# install programs
#
	for f in $(G3_PROGRAMS) ; do \
		$(INSTALL) -s -m 755 $$f $(BINDIR) ; \
	done
#
# make symlink for "g3topbm" (so third-party fax viewers won't fail)
#
	-cd $(BINDIR) ; \
	test -x g3topbm || ln -s g32pbm g3topbm
