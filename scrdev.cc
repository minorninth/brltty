/*
 * BRLTTY - Access software for Unix for a blind person
 *          using a soft Braille terminal
 *
 * Nikhil Nair <nn201@cus.cam.ac.uk>
 * Nicolas Pitre <nico@cam.org>
 * Stephane Doyon <doyons@jsp.umontreal.ca>
 *
 * Version 1.0.2, 17 September 1996
 *
 * Copyright (C) 1995, 1996 by Nikhil Nair and others.  All rights reserved.
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * This software is maintained by Nikhil Nair <nn201@cus.cam.ac.uk>.
 */

/* scrdev.cc - screen types library
 * $Id: scrdev.cc,v 1.3 1996/09/24 01:04:27 nn201 Exp $
 *
 * Note: Although C++, this code requires no standard C++ library.
 * This is important as BRLTTY *must not* rely on too many
 * run-time shared libraries, nor be a huge executable.
 */

#define SCR_C 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include "scrdev.h"
#include "config.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))


inline int
LiveScreen::open (void)
{
  if ((fd = ::open (SCRDEV, O_RDONLY)) == -1)
    return 1;
  return 0;
}


void
LiveScreen::getstat (scrstat &stat)
{
  unsigned char buffer[4];

  lseek (fd, 0, SEEK_SET);      /* go to start of file */
  read (fd, buffer, 4);         /* get screen status bytes */
  stat.rows = buffer[0];
  stat.cols = buffer[1];
  stat.posx = buffer[2];
  stat.posy = buffer[3];
}


unsigned char *
LiveScreen::getscr (winpos pos, unsigned char *buffer, short mode)
{
  scrstat stat;                 /* screen statistics */
  off_t start;                  /* start offset */
  size_t linelen;               /* number of bytes to read for one */
				/* complete line */
  char linebuf[512];            /* line buffer; larger than is needed */

  getstat (stat);
  if (pos.left < 0 || pos.top < 0 || pos.width < 1 || pos.height < 1 \
      || mode < 0 || mode > 1 || pos.left + pos.width > stat.cols \
      || pos.top + pos.height > stat.rows)
    return NULL;
  start = 4 + (pos.top * stat.cols + pos.left) * 2 + mode;
  linelen = 2 * pos.width - 1;
  for (int i = 0; i < pos.height; i++)
    {
      lseek (fd, start + i * stat.cols * 2, SEEK_SET);
      read (fd, linebuf, linelen);
      for (int j = 1; j < pos.width; linebuf[j] = linebuf[j * 2], j++);
      memcpy (buffer + i * pos.width, linebuf, pos.width);
    }
  return buffer;
}


inline void
LiveScreen::close (void)
{
  ::close (fd);
}


inline
FrozenScreen::FrozenScreen ()
{
  text = 0;
}


int
FrozenScreen::open (LiveScreen &live)
{
  live.getstat (stat);
  if (!(text = new unsigned char[stat.rows * stat.cols]))
    return 1;
  if (!(attrib = new unsigned char[stat.rows * stat.cols]))
    {
      delete text;
      return 1;
    }
  if (!live.getscr ((winpos) { 0, 0, stat.cols, stat.rows }, text, SCR_TEXT) \
      || !live.getscr ((winpos) { 0, 0, stat.cols, stat.rows }, attrib, \
		       SCR_ATTRIB))
    {
      delete text;
      text = 0;
      delete attrib;
      return 2;
    }
  return 0;
}


void
FrozenScreen::getstat (scrstat &stat2)
{
  stat2 = stat;
}


unsigned char *
FrozenScreen::getscr (winpos pos, unsigned char *buffer, short mode)
{
  unsigned char *scrn;

  if (pos.left < 0 || pos.top < 0 || pos.width < 1 || pos.height < 1 \
      || mode < 0 || mode > 1 || pos.left + pos.width > stat.cols \
      || pos.top + pos.height > stat.rows)
    return NULL;
  scrn = (mode == SCR_TEXT) ? text : attrib;
  for (int i = 0; i < pos.height; i++)
    memcpy (buffer + i * pos.width, scrn + (pos.top + i)* stat.cols + \
	    pos.left, pos.width);
  return buffer;
}


inline void
FrozenScreen::close (void)
{
  if (text)
    {
      delete text;
      delete attrib;
    }
}


inline
HelpScreen::HelpScreen ()
{
  fd = -1;
  psz = 0;
  page = 0;
  buffer = 0;
}


int
HelpScreen::gethelp (void)
{
  long bufsz = 0;		// total length of formatted help screens
  unsigned char maxcols = 0;	// width of the widest page
  unsigned char linelen;	// length of an individual line
  short i, j, k;		// loop counters

  if ((fd = ::open (HLPFILE, O_RDONLY)) == -1)
    return 1;
  if (read (fd, &numpages, sizeof numpages) != sizeof numpages || \
      numpages < 1)
    goto failure;
  if (!(psz = new pageinfo[numpages]))
    goto failure;
  if (!(page = new unsigned char *[numpages]))
    goto failure;
  for (i = 0; i < numpages; i++)
    if (read (fd, &psz[i], sizeof (pageinfo)) != sizeof (pageinfo))
      goto failure;
  for (i = 0; i < numpages; i++)
    {
      bufsz += psz[i].rows * psz[i].cols;
      maxcols = MAX (maxcols, psz[i].cols);
    }
  if (!(buffer = new unsigned char[bufsz + 2]))
    goto failure;
  page[0] = buffer;
  for (i = 0; i < numpages - 1; i++)
    page[i + 1] = page[i] + psz[i].rows * psz[i].cols;
  for (i = 0; i < numpages; i++)
    for (j = 0; j < psz[i].rows; j++)
      {
	if (read (fd, &linelen, 1) != 1 || \
	    !(linelen == 0 || read (fd, page[i] + j * psz[i].cols, linelen) \
	      == linelen))
	  goto failure;
	for (k = linelen; k < psz[i].cols; k++)
	  page[i][j * psz[i].cols + k] = ' ';
      }
  ::close (fd);
  return 0;

 failure:
  if (buffer)
    delete buffer;
  if (page)
    delete page;
  if (psz)
    delete psz;
  psz = 0;
  page = 0;
  buffer = 0;
  ::close (fd);
  fd = -1;
  return 2;
}


inline void
HelpScreen::setscrno (short x)
{
  if (fd == -1 || x >= 0 && x < numpages)
    scrno = x;
}


short
HelpScreen::numscreens (void)
{
  return fd != -1 ? numpages : 0;
}


inline int
HelpScreen::open (void)
{
  if (fd == -1 && gethelp ())
    return 1;
  if (scrno < 0 || scrno >= numpages)
    scrno = 0;
  return 0;
}


void
HelpScreen::getstat (scrstat &stat)
{
  stat.posx = stat.posy = 0;
  stat.cols = psz[scrno].cols;
  stat.rows = psz[scrno].rows;
}


unsigned char *
HelpScreen::getscr (winpos pos, unsigned char *buffer, short mode)
{
  if (pos.left < 0 || pos.top < 0 || pos.width < 1 || pos.height < 1 \
      || mode < 0 || mode > 1 || pos.left + pos.width > psz[scrno].cols \
      || pos.top + pos.height > psz[scrno].rows)
    return NULL;
  if (mode == SCR_ATTRIB)
    {
      for (int i = 0; i < pos.width * pos.height; buffer[i++] = 0x07);
      return buffer;
    }
  for (int i = 0; i < pos.height; i++)
    memcpy (buffer + i * pos.width, page[scrno] + (pos.top + i) * \
	    psz[scrno].cols + pos.left, pos.width);
  return buffer;
}


inline void
HelpScreen::close (void)
{
  if (fd != -1)
    {
      delete psz;
      delete page;
      delete buffer;
      fd = -1;
    }
}
