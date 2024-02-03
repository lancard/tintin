/*
TinTin++
Copyright (C) 2001 Davin Chan, Robert Ellsworth, etc. (See CREDITS file)

This program is protected under the GNU GPL (See COPYING)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/
/* New for v2.0: readline support -- daw */

#include "config.h"

#include <stdio.h>

#include "include/rlvt100.h"

/*
 * i tried to put all the stuff that is terminal-specific in one file,
 * just in case we decide to support other terminals someday or something.
 * [forget switching to curses, it and readline do *not* get along.]
 * 
 * there is still some terminal-specific stuff in other files, though.
 * (highlight.c springs to mind)
 */

#define	ESCAPE 27

/* don't forget to do a 'fflush(stdout);' after each of these commands! */

void save_pos(void)
{
  printf("%c7", ESCAPE); 
}

void restore_pos(void)
{
  printf("%c8", ESCAPE); 
}

void goto_rowcol(int row, int col)
{
  printf("%c[%d;%df", ESCAPE, row, col);
}

void erase_screen(void)
{
  printf("%c[2J", ESCAPE);
}

void erase_toeol(void)
{
  printf("%c[K", ESCAPE);
}

void reset(void)
{
  printf("%cc", ESCAPE);
}

void scroll_region(int top, int bottom)
{
  printf("%c[%d;%dr", ESCAPE, top, bottom);
}
