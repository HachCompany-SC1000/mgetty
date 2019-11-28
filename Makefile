# Makefile for the mgetty fax package
#
# SCCS-ID: $Id: Makefile,v 4.56 2002/12/16 13:08:23 gert Exp $ (c) Gert Doering
#
# this is the C compiler to use (on SunOS, the standard "cc" does not
# grok my code, so please use gcc there. On ISC 4.0, use "icc".).
CC=gcc
#CC=cc
#
#### C Compiler Flags ####
#
# For SCO Unix 3.2v4, it may be necessary to define -D__STDC__=0
# If you have problems with dial-in / dial-out on the same line (e.g.
# 'cu' telling you "CAN'T ACCESS DEVICE"), you should try -DBROKEN_SCO_324
# If the compiler barfs about my getopt() prototype, change it (mgetty.h)
# If the linker cannot find "syslog()" (compiled with SYSLOG defined),
# link "-lsocket".
#
# On SCO Unix 3.2.2, select(S) refuses to sleep less than a second,
# so use poll(S) instead. Define -DUSE_POLL
#
# For Systems with a "login uid" (man setluid()), define -DSECUREWARE
# (All SCO Systems have this). Maybe you've to link some special library
# for it, on SCO it's "-lprot_s".
#
#
# Add "-DSVR4" to compile on System V Release 4 unix
# For SVR4.2, add "-DSVR4 -DSVR42" to the CFLAGS line.
#
# For Linux, you don't have to define anything
#
# For SunOS 4.x, please define -Dsunos4.
#   (We can't use "#ifdef sun" because that's defined on solaris as well)
#   If you use gcc, add -Wno-missing-prototypes, our code is OK, but
#   the Sun4 header files lack a lot standard definitions...
#   Be warned, Hardware handshake (and serial operations in general)
#   work a lot more reliably with patch 100513-05 (jumbo tty patch)!
#
# For Solaris 2.x, please define -Dsolaris2, which will automatically
#   #define SVR4.
#
# Add "-DISC" to compile on Interactive Unix. For posixized ISC 4.0 you
# may have to add -D_POSIX_SOURCE
#
# For IBM AIX 4.x, no changes should be necessary. For AIX 3.x, it might
# be necessary to add -D_ALL_SOURCE and -DUSE_POLL to the CFLAGS line.
# [If you experience "strange" problems with AIX, report to me...!]
#
# Add "-D_3B1_ -DUSE_READ -D_NOSTDLIB_H -DSHORT_FILENAMES" to compile
# on the AT&T 3b1 machine -g.t.
#
# Add "-DMEIBE" to compile on the Panasonic (Matsushita Electric
#	industry) BE workstation
#
# When compiling on HP/UX, make sure that the compiler defines _HPUX_SOURCE,
#     if it doesn't, add "-D_HPUX_SOURCE" to the CFLAGS line.
#
# On NeXTStep, add "-DNEXTSGTTY -DBSD". To compile vgetty or if you are
#     brave, you have to use "-posix -DBSD".
#
# On the MIPS RiscOS, add "-DMIPS -DSVR4 -systype svr4" (the other
#     subsystems are too broken. Watch out that *all* programs honour
#     the SVR4 locking convention, "standard" UUCP and CU do not!)
#
# For (otherwise not mentioned) systems with BSD utmp handling, define -DBSD
#
# Add "-D_NOSTDLIB_H" if you don't have <stdlib.h>
#
# Add "-DPOSIX_TERMIOS" to use POSIX termios.h, "-DSYSV_TERMIO" to use
#     the System 5 termio.h style. (Default set in tio.h)
#
# For machines without the select() call:
#     Add "-DUSE_POLL" if you don't have select, but do have poll
#     Add "-DUSE_READ" if you don't have select or poll (ugly)
#
# For older SVR systems (with a filename length restriction to 14
#     characters), define -DSHORT_FILENAMES
#
# For Systems that default to non-interruptible system calls (symptom:
# timeouts in do_chat() don't work reliably) but do have siginterrupt(),
# define -DHAVE_SIGINTERRUPT. This is the default if BSD is defined.
#
# For Systems with broken termios / VMIN/VTIME mechanism (symptom: mgetty
# hangs in "waiting for line to clear"), define -DBROKEN_VTIME. Notably
# this hits FreeBSD 0.9 and some SVR4.2 users...
#
# If you don't have *both* GNU CC and GNU AS, remove "-pipe"
#
# Disk statistics:
#
# The following macros select one of 5 different variants of partition
# information grabbing.  AIX, Linux, 3b1 and SVR4 systems will figure
# this out themselves.  You can test whether you got this right by
# running "make testdisk".  If it fails, consult getdisk.c for
# further instructions.
#
#	    BSDSTATFS     - BSD/hp-ux/SunOS/Dynix/vrios
#			    2-parameter statfs()
#	    ULTRIXSTATFS  - Ultrix wacko statfs
#	    SVR4	  - SVR4 statvfs()
#	    SVR3	  - SVR3/88k/Apollo/SCO 4-parameter statfs()
#	    USTAT	  - ustat(), no statfs etc.
#
# If you want to support incoming FidoNet calls, add -DFIDO.
# If you want to auto-detect incoming PPP calls (with PAP authorization),
# add -DAUTO_PPP. Not needed if PPP callers want to get a real "login:"
# prompt first. Don't forget to activate the /AutoPPP/ line in login.config!
#
#CFLAGS=-Wall -O2 -pipe -DSECUREWARE -DUSE_POLL
#
#CFLAGS=-O2 -Wall -pipe -DAUTO_PPP
CFLAGS=-O0 -ggdb -Wall -pipe -DAUTO_PPP
#--------- SC1000 -------
CFLAGS += -fms-extensions
#------------------------

#CFLAGS=-O -DSVR4
#CFLAGS=-O -DSVR4 -DSVR42
#CFLAGS=-O -DUSE_POLL
#CFLAGS=-Wall -g -O2 -pipe
# 3B1: You can remove _NOSTDLIB_H and USE_READ if you have the
# networking library and gcc.
#CFLAGS=-D_3B1_ -D_NOSTDLIB_H -DUSE_READ -DSHORT_FILENAMES
#CFLAGS=-std -DPOSIX_TERMIOS -O2 -D_BSD -DBSD	# for OSF/1 (w/ /bin/cc)
#CFLAGS=-DNEXTSGTTY -DBSD -O2			# for NeXT with sgtty (better!)
#CFLAGS=-posix -DUSE_VARARGS -DBSD -O2		# for NeXT with POSIX
#CFLAGS=-D_HPUX_SOURCE -Aa -DBSDSTATFS		# for HP-UX 9.x
#CFLAGS=-cckr -D__STDC__ -O -DUSE_READ 		# for IRIX 5.2 and up


#
# LDFLAGS specify flags to pass to the linker. You could specify
# 	special link modes, binary formats, whatever...
#
# For the 3B1, add "-s -shlib". For other systems, nothing is needed.
#
# LIBS specify extra libraries to link to the programs
#       (do not specify the libraries in the LDFLAGS statement)
#
# To use the "setluid()" function on SCO, link "-lprot", and to use
# the "syslog()" function, link "-lsocket".
#
# For SVR4(.2) and Solaris 2, you may need "-lsocket -lnsl" for syslog().
#
# For ISC, add "-linet -lpt" (and -lcposix, if necessary)
#
# For Sequent Dynix/ptx, you have to add "-lsocket"
#
# For OSF/1, add "-lbsd".
#
# On SCO Xenix, add -ldir -lx
#
# For FreeBSD and NetBSD, add "-lutil" if the linker complains about
# 	"utmp.o: unresolved symbod _login"
# For Linux, add "-lutil" if the linker complains about "updwtmp".
#
#--------- SC1000 -------
LDFLAGS= -L../lib -L../SC1000/lib
LIBS=-lrt -lm -lUiManager -lMbManager -lLicenseManager -lLink2scCntrl
#------------------------
#LIBS=-lprot -lsocket				# SCO Unix
#LIBS=-lsocket
#LIBS=-lbsd					# OSF/1
#LIBS=-lutil					# FreeBSD or Linux/GNU libc2
#LDFLAGS=-posix					# NeXT with POSIX
#LDFLAGS=-s -shlib				# 3B1
#
#
# the following things are mainly used for ``make install''
#
#
# program to use for installing files
#
# "-o <username>" sets the username that will own the binaries after installing.
# "-g <group>" sets the group
# "-c" means "copy" (as opposed to "move")
#
# if your systems doesn't have one, use the shell script that I provide
# in "inst.sh" (taken from X11R5). Needed on IRIX5.2
INSTALL=install -c -o bin -g bin
#INSTALL=install -c -o root -g wheel		# NeXT/BSD
#INSTALL=/usr/ucb/install -c -o bin -g bin	# AIX, Solaris 2.x
#INSTALL=installbsd -c -o bin -g bin		# OSF/1, AIX 4.1, 4.2
#INSTALL=/usr/bin/X11/bsdinst -c -o bin 	# IRIX
#
# prefix, where most (all?) of the stuff lives, usually /usr/local or /usr
#
prefix=/usr/local
#
# prefix for all the spool directories (usually /usr/spool or /var/spool)
#
spool=/var/spool
#
# where the mgetty + sendfax binaries live (used for "make install")
#
SBINDIR=$(prefix)/sbin
#
# where the user executable binaries live
#
BINDIR=$(prefix)/bin
#
# where the font+coverpage files go
#
LIBDIR=$(prefix)/lib/mgetty+sendfax
#
# where the configuration files (*.config, aliases, fax.allow/deny) go to
#
CONFDIR=$(prefix)/etc/mgetty+sendfax
#CONFDIR=/etc/default/
#
#
# where mgetty PID files (mgetty.pid) go to
# (the faxrunqd PID file lives in FAX_SPOOL_OUT/ due to permission reasons)
#
VARRUNDIR=/var/run
#
# the fax spool directory
#
FAX_SPOOL=$(spool)/fax
FAX_SPOOL_IN=$(FAX_SPOOL)/incoming
FAX_SPOOL_OUT=$(FAX_SPOOL)/outgoing
#
# the user that owns the "outgoing fax queue" (FAX_SPOOL_OUT)
#
# faxrunq and faxrunqd should run under this user ID, and nothing else.
# This user needs access to the modems of course.
#
# (it's possible to run faxrunq(d) as root, but the FAX_OUT_USER
#  MUST NOT BE root or any other privileged account).
#
#FAX_OUT_USER=fax
FAX_OUT_USER=root
#
#
# Where section 1 manual pages should be placed
MAN1DIR=$(prefix)/man/man1
#
# Where section 4 manual pages (mgettydefs.4) should be placed
MAN4DIR=$(prefix)/man/man4
#
# Section 5 man pages (faxqueue.5)
MAN5DIR=$(prefix)/man/man5
#
# Section 8 man pages (sendfax.8)
MAN8DIR=$(prefix)/man/man8
#
# Where the GNU Info-Files are located
#
INFODIR=$(prefix)/info
#
#
# A shell that understands bourne-shell syntax
#  Usually this will be /bin/sh or /usr/bin/sh, but bash or ksh are fine.
#  (on some ultrix systems, you may need /bin/sh5 here)
#
SHELL=/bin/sh
#
# If you have problems with the awk-programs in the fax/ shell scripts,
# try using "nawk" or "gawk" (or whatever works...) here
# needed on most SunOS/Solaris/ISC/NeXTStep versions.
#
AWK=awk
#
# A few (very few) programs want Perl, preferably Perl5. This define
# tells them where to find it. You can use everything except "faxrunqd"
# and the "tkperl" frontends without PERL, so don't worry if you don't
# have it.
# If you specify command line arguments (-w), don't forget the quotes!
PERL="/usr/bin/perl -w"
#
# If you have Perl with TK extentions, define it here. This may be the
# same as PERL=... above, or different, if you have TkPerl statically
# linked.
TKPERL=/usr/bin/tkperl
#
#
# An echo program that understands escapes like "\n" for newline or
# "\c" for "no-newline-at-end". On SunOS, this is /usr/5bin/echo, in the
# bash, it's "echo -e"
# (don't forget the quotes, otherwise compiling mksed will break!!)
#
# If you do not have *any* echo program at all that will understand "\c",
# please use the "mg.echo" program provided in the compat/ subdirectory.
# Set ECHO="mg.echo" and INSTALL_MECHO to mg.echo
#
ECHO="echo"
#
# INSTALL_MECHO=mg.echo

#
# for mgetty, that's it. If you want to use the voice
# extentions, go ahead (don't forget to read the manual!)

# To maintain security, I recommend creating a new group for
# users who are allowed to manipulate the recorded voice messages.
PHONE_GROUP=phone
PHONE_PERMS=770

# Add -DNO_STRSTR to CFLAGS if you don't have strstr().

# create hard/soft links (-s will be added by vgetty Makefile)
LN=ln
#LN=/usr/5bin/ln

RM=rm
MV=mv

#
# Nothing to change below this line ---------------------------------!
#
MR=1.1
SR=30
DIFFR=1.1.29
#
#
OBJS=mgetty.o logfile.o do_chat.o locks.o utmp.o logname.o login.o \
     mg_m_init.o modem.o faxrec.o ring.o \
     faxlib.o faxsend.o faxrecp.o class1.o class1lib.o faxhng.o \
     io.o gettydefs.o tio.o cnd.o getdisk.o goodies.o \
     config.o conf_mg.o do_stat.o ttyResource.o \
     MonotonicTime.o

all:	bin-all

bin-all: mgetty

# a few C files need extra compiler arguments

mgetty.o : mgetty.c syslibs.h mgetty.h ugly.h policy.h tio.h fax_lib.h \
	config.h mg_utmp.h Makefile
	$(CC) $(CFLAGS) -DVARRUNDIR=\"$(VARRUNDIR)\" -c mgetty.c

conf_mg.o : conf_mg.c mgetty.h ugly.h policy.h syslibs.h \
	config.h conf_mg.h Makefile
	$(CC) $(CFLAGS) -DFAX_SPOOL_IN=\"$(FAX_SPOOL_IN)\" \
		-DCONFDIR=\"$(CONFDIR)\" -c conf_mg.c

login.o : login.c mgetty.h ugly.h config.h policy.h mg_utmp.h  Makefile
	$(CC) $(CFLAGS) -DCONFDIR=\"$(CONFDIR)\" -c login.c


# here are the binaries...

mgetty: $(OBJS)
	$(CC) -o mgetty $(OBJS) $(LDFLAGS) $(LIBS)


#	pgp -sab mgetty$(MR).$(SR).tar.gz

clean:
	rm -f *.o getdisk compat/*.o newslock mgetty
	cd g3 ; $(MAKE) clean
	cd fax ; $(MAKE) clean
	cd tools ; $(MAKE) clean
	cd callback ; $(MAKE) clean
	cd contrib ; $(MAKE) clean
	cd doc ; $(MAKE) clean
	cd voice ; $(MAKE) clean

realclean: clean

fullclean:
	rm -f *.o compat/*.o mgetty sendfax testgetty getdisk \
			mksed sedscript newslock *~ \
			sendfax.config mgetty.config login.config
	cd g3 ; $(MAKE) fullclean
	cd fax ; $(MAKE) fullclean
	cd tools ; $(MAKE) fullclean
	cd callback ; $(MAKE) fullclean
	cd contrib ; $(MAKE) fullclean
	cd doc ; $(MAKE) fullclean
	cd voice ; $(MAKE) fullclean
