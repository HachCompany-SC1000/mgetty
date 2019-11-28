/* Fax file input processing
   Copyright (C) 1990, 1995  Frank D. Cringle.

This file is part of viewfax - g3/g4 fax processing software.
     
viewfax is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
     
This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.
     
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include "faxexpand.h"

#define	FAXMAGIC	"\000PC Research, Inc\000\000\000\000\000\000"

/* Enter an argument in the linked list of pages */
struct pagenode *
notefile(char *name)
{
    struct pagenode *new = (struct pagenode *) xmalloc(sizeof *new);

    *new = defaultpage;
    if (firstpage == NULL)
	firstpage = new;
    new->prev = lastpage;
    new->next = NULL;
    if (lastpage != NULL)
	lastpage->next = new;
    lastpage = new;
    new->pathname = name;
    if ((new->name = strrchr(new->pathname, '/')) != NULL)
	new->name++;
    else
	new->name = new->pathname;

    if (new->width == 0)
	new->width = 1728;
    if (new->vres < 0)
	new->vres = !(new->name[0] == 'f' && new->name[1] == 'n');
    new->extra = NULL;
    return new;
}

static t32bits
get4(unsigned char *p, int endian)
{
    return endian ? (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3] :
	p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24);
}

static int
get2(unsigned char *p, int endian)
{
    return endian ? (p[0]<<8)|p[1] : p[0]|(p[1]<<8);
}

/* generate pagenodes for the images in a tiff file */
int
notetiff(char *name)
{
    FILE *tf;
    unsigned char header[8];
    static const char littleTIFF[4] = "\x49\x49\x2a\x00";
    static const char bigTIFF[4] = "\x4d\x4d\x00\x2a";
    int endian;
    t32bits IFDoff;
    struct pagenode *pn = NULL;

    if ((tf = fopen(name, "r")) == NULL) {
	perror(name);
	return 0;
    }

    if (fread(header, 8, 1, tf) == 0) {
    nottiff:
	fclose(tf);
	(void) notefile(name);
	return 0;
    }
    if (memcmp(header, &littleTIFF, 4) == 0)
	endian = 0;
    else if (memcmp(header, &bigTIFF, 4) == 0)
	endian = 1;
    else
	goto nottiff;
    IFDoff = get4(header+4, endian);
    if (IFDoff & 1)
	goto nottiff;
    do {			/* for each page */
	unsigned char buf[8];
	unsigned char *dir = NULL;
	unsigned char *dp;
	int ndirent;
	pixnum iwidth = defaultpage.width ? defaultpage.width : 1728;
	pixnum iheight = defaultpage.height ? defaultpage.height : 2339;
	int inverse = defaultpage.inverse;
	int lsbfirst = 0;
	int t4opt = 0, comp = 0;
	int orient = defaultpage.orient;
	double yres = defaultpage.vres ? 196.0 : 98.0;
	struct strip *strips = NULL;
	unsigned long rowsperstrip = 0;
	int nstrips = 1;

	if (fseek(tf, IFDoff, SEEK_SET) < 0) {
	realbad:
	    fprintf(stderr, "%s:%s: invalid tiff file\n", ProgName, name);
	bad:
	    if (strips)
		free(strips);
	    if (dir)
		free(dir);
	    fclose(tf);
	    return 1;
	}
	if (fread(buf, 2, 1, tf) == 0)
	    goto realbad;
	ndirent = get2(buf, endian);
	dir = (unsigned char *) xmalloc(12*ndirent+4);
	if (fread(dir, 12*ndirent+4, 1, tf) == 0)
	    goto realbad;
	for (dp = dir; ndirent; ndirent--, dp += 12) {
	    /* for each directory entry */
	    int tag, ftype;
	    t32bits count, value = 0;
	    tag = get2(dp, endian);
	    ftype = get2(dp+2, endian);
	    count = get4(dp+4, endian);
	    switch(ftype) {	/* value is offset to list if count*size > 4 */
	    case 3:		/* short */
		value = get2(dp+8, endian);
		break;
	    case 4:		/* long */
		value = get4(dp+8, endian);
		break;
	    case 5:		/* offset to rational */
		value = get4(dp+8, endian);
		break;
	    }
	    switch(tag) {
	    case 256:		/* ImageWidth */
		iwidth = value;
		break;
	    case 257:		/* ImageLength */
		iheight = value;
		break;
	    case 259:		/* Compression */
		comp = value;
		break;
	    case 262:		/* PhotometricInterpretation */
		inverse ^= (value == 1);
		break;
	    case 266:		/* FillOrder */
		lsbfirst = (value == 2);
		break;
	    case 273:		/* StripOffsets */
		nstrips = count;
		strips = (struct strip *) xmalloc(count * sizeof *strips);
		if (count == 1 || (count == 2 && ftype == 3)) {
		    strips[0].offset = value;
		    if (count == 2)
			strips[1].offset = get2(dp+10, endian);
		    break;
		}
		if (fseek(tf, value, SEEK_SET) < 0)
		    goto realbad;
		for (count = 0; count < nstrips; count++) {
		    if (fread(buf, (ftype == 3) ? 2 : 4, 1, tf) == 0)
			goto realbad;
		    strips[count].offset = (ftype == 3) ?
			get2(buf, endian) : get4(buf, endian);
		}
		break;
	    case 274:		/* Orientation */
		switch(value) {
		default:	/* row0 at top,    col0 at left   */
		    orient = 0;
		    break;
		case 2:		/* row0 at top,    col0 at right  */
		    orient = TURN_M;
		    break;
		case 3:		/* row0 at bottom, col0 at right  */
		    orient = TURN_U;
		    break;
		case 4:		/* row0 at bottom, col0 at left   */
		    orient = TURN_U|TURN_M;
		    break;
		case 5:		/* row0 at left,   col0 at top    */
		    orient = TURN_M|TURN_L;
		    break;
		case 6:		/* row0 at right,  col0 at top    */
		    orient = TURN_U|TURN_L;
		    break;
		case 7:		/* row0 at right,  col0 at bottom */
		    orient = TURN_U|TURN_M|TURN_L;
		    break;
		case 8:		/* row0 at left,   col0 at bottom */
		    orient = TURN_L;
		    break;
		}
		break;
	    case 278:		/* RowsPerStrip */
		rowsperstrip = value;	
		break;
	    case 279:		/* StripByteCounts */
		if (count != nstrips) {
		    fprintf(stderr, "%s:%s StripsPerImage tag273=%d, tag279=%ld\n",
			    ProgName, name, nstrips, count);
		    goto realbad;
		}
		if (count == 1 || (count == 2 && ftype == 3)) {
		    strips[0].size = value;
		    if (count == 2)
			strips[1].size = get2(dp+10, endian);
		    break;
		}
		if (fseek(tf, value, SEEK_SET) < 0)
		    goto realbad;
		for (count = 0; count < nstrips; count++) {
		    if (fread(buf, (ftype == 3) ? 2 : 4, 1, tf) == 0)
			goto realbad;
		    strips[count].size = (ftype == 3) ?
			get2(buf, endian) : get4(buf, endian);
		}
		break;
	    case 283:		/* YResolution */
		if (fseek(tf, value, SEEK_SET) < 0 ||
		    fread(buf, 8, 1, tf) == 0)
		    goto realbad;
		yres = get4(buf, endian) / get4(buf+4, endian);
		break;
	    case 292:		/* T4Options */
		t4opt = value;
		break;
	    case 293:		/* T6Options */
		/* later */
		break;
	    case 296:		/* ResolutionUnit */
		if (value == 3)
		    yres *= 2.54;
		break;
	    }
	}
	IFDoff = get4(dp, endian);
	free(dir);
	dir = NULL;
	if (comp < 2 || comp > 4) {
	    fprintf(stderr, "%s:%s: this version only handles fax files\n",
		ProgName, name);
	    goto bad;
	}
	pn = notefile(name);
	pn->nstrips = nstrips;
	pn->rowsperstrip = nstrips > 1 ? rowsperstrip : iheight;
	pn->strips = strips;
	pn->width = iwidth;
	pn->height = iheight;
	pn->inverse = inverse;
	pn->lsbfirst = lsbfirst;
	pn->orient = orient;
	pn->vres = (yres > 150); /* arbitrary threshold for fine resolution */
	if (comp == 2)
	    pn->expander = MHexpand;
	else if (comp == 3)
	    pn->expander = (t4opt & 1) ? g32expand : g31expand;
	else
	    pn->expander = g4expand;
    } while (IFDoff);
    fclose(tf);
    return 1;
}

/* report error and remove bad file from the list */
static void
badfile(struct pagenode *pn)
{
    struct pagenode *p;

    if (errno)
	perror(pn->pathname);
    if (pn == firstpage) {
	if (pn->next == NULL)
	    exit(1);
	firstpage = thispage = firstpage->next;
	firstpage->prev = NULL;
    }
    else
	for (p = firstpage; p; p = p->next)
	    if (p->next == pn) {
		thispage = p;
		p->next = pn->next;
		if (pn->next)
		    pn->next->prev = p;
		break;
	    }
    if (pn) free(pn);
}

/* rearrange input bits into t16bits lsb-first chunks */
static void
normalize(struct pagenode *pn, int revbits, int swapbytes, size_t length)
{
    t32bits *p = (t32bits *) pn->data;

    switch ((revbits<<1)|swapbytes) {
    case 0:
	break;
    case 1:
	for ( ; length; length -= 4) {
	    t32bits t = *p;
	    *p++ = ((t & 0xff00ff00) >> 8) | ((t & 0x00ff00ff) << 8);
	}
	break;
    case 2:
	for ( ; length; length -= 4) {
	    t32bits t = *p;
	    t = ((t & 0xf0f0f0f0) >> 4) | ((t & 0x0f0f0f0f) << 4);
	    t = ((t & 0xcccccccc) >> 2) | ((t & 0x33333333) << 2);
	    *p++ = ((t & 0xaaaaaaaa) >> 1) | ((t & 0x55555555) << 1);
	}
	break;
    case 3:
	for ( ; length; length -= 4) {
	    t32bits t = *p;
	    t = ((t & 0xff00ff00) >> 8) | ((t & 0x00ff00ff) << 8);
	    t = ((t & 0xf0f0f0f0) >> 4) | ((t & 0x0f0f0f0f) << 4);
	    t = ((t & 0xcccccccc) >> 2) | ((t & 0x33333333) << 2);
	    *p++ = ((t & 0xaaaaaaaa) >> 1) | ((t & 0x55555555) << 1);
	}
    }
}


/* get compressed data into memory */
unsigned char *
getstrip(struct pagenode *pn, int strip)
{
    int fd, n;
    size_t offset, roundup;
    struct stat sbuf;
    unsigned char *Data;
    union { t16bits s; unsigned char b[2]; } so;
#define ShortOrder	so.b[1]

    so.s = 1;
    if ((fd = open(pn->pathname, O_RDONLY, 0)) < 0) {
	badfile(pn);
	return NULL;
    }

    if (pn->strips == NULL) {
	if (fstat(fd, &sbuf) != 0) {
	    close(fd);
	    badfile(pn);
	    return NULL;
	}
	offset = 0;
	pn->length = sbuf.st_size;
    }
    else if (strip < pn->nstrips) {
	offset = pn->strips[strip].offset;
	pn->length = pn->strips[strip].size;
    }
    else {
	fprintf(stderr, "%s:%s: trying to expand too many strips\n",
		ProgName, pn->pathname);
	close(fd);
	badfile(pn);
	return NULL;
    }
    if (!pn->length) {
	fprintf(stderr, "%s:%s: trying to expand a null strip\n",
		ProgName, pn->pathname);
	close(fd);  
	badfile(pn);
	return NULL;
    }

    /* round size to full boundary plus t32bits */
    roundup = (pn->length + 7) & ~3;

    Data = (unsigned char *) xmalloc(roundup);
    /* clear the last 2 t32bits, to force the expander to terminate
       even if the file ends in the middle of a fax line  */
    *((t32bits *) Data + roundup/4 - 2) = 0;
    *((t32bits *) Data + roundup/4 - 1) = 0;

    /* we expect to get it in one gulp... */
    if (lseek(fd, offset, SEEK_SET) < 0 ||
	(n = read(fd, Data, pn->length)) != pn->length) {
	fprintf(stderr, "%s: expected %d bytes, got %d\n",
		pn->pathname, pn->length, n);
	badfile(pn);
	free(Data);
	close(fd);
	return NULL;
    }
    close(fd);

    pn->data = (t16bits *) Data;
    if (pn->strips == NULL && memcmp(Data, FAXMAGIC, sizeof(FAXMAGIC)) == 0) {
	/* handle ghostscript / PC Research fax file */
	if (Data[24] != 1 || Data[25] != 0)
	    printf("%s: only first page of multipage file %s will be shown\n",
		   ProgName, pn->pathname);
	pn->length -= 64;
	pn->vres = Data[29];
	pn->data += 32;
	roundup -= 64;
    }

    normalize(pn, !pn->lsbfirst, ShortOrder, roundup);
    if (pn->height == 0)
	pn->height = G3count(pn, pn->expander == g32expand);
    if (pn->height == 0) {
	fprintf(stderr, "%s: no fax found in file %s\n", ProgName,
		pn->pathname);
	errno = 0;
	badfile(pn);
	free(Data);
	return NULL;
    }
    if (pn->strips == NULL)
	pn->rowsperstrip = pn->height;
    if (verbose && strip == 0)
	printf("%s:\n\twidth = %lu\n\theight = %lu\n\tresolution = %s\n",
	       pn->name, (unsigned long) pn->width, (unsigned long) pn->height,
	       pn->vres ? "fine" : "normal");
    return Data;
}
