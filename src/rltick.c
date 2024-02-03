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

/*
 * this is a reimplementation of the standard tickcounter, *without* alarm()!
 * god i hate alarm() -- it screws everything up, and isn't portable,
 * and tintin was calling alarm() every second!  blech.
 */

#include "config.h"
#include "tintin.h"

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include "include/main.h"
#include "include/rl.h"
#include "include/rltick.h"
#include "include/ticks.h"

int timetilltick(void)
{
  int ttt;

  ttt = (time(0) - time0) % tick_size;
  ttt = (tick_size - ttt) % tick_size;
  return(ttt);
}

/*
 * returns how long before the next event, in seconds.
 * (i.e. if we're 13 seconds till tick in some session,
 * we return 3, because in 3 seconds we'll need to warn
 * the user that they are 10 seconds short of the tick.)
 * 
 * also prints the tick warnings, by the way. :-)
 * 
 * bug: if you suspend tintin++ for a few minutes, then
 * bring it back, you get lots of tick warnings.  is this
 * the desired behavior?
 */
int checktick(void)
{
  static int last = -1, ttt = -1; /* ttt = time to tick */
  int now, found = 0;
  struct session *s;

  if(time0 <= 0)
    return(100);	/* big number */

  now = time(0);

  if(last > 0)
    while(last <= now) {
      ttt = (++last - time0) % tick_size;
      ttt = (tick_size - ttt) % tick_size;
      if(!ttt || ttt == 10)
	for(s = sessionlist; s; s = s->next)
	  if(s->tickstatus)
	    {
	      if (!ttt)
		tintin_puts("#TICK!!!", s);
	      else if (show_pretick)
		tintin_puts("#10 SECONDS TO TICK!!!", s);
	      /*	    tintin_puts(!ttt ? "#TICK!!!" : "#10 SECONDS TO TICK!!!", s); */
	    }
    }
  else {
    last = now+1;
    ttt = (now - time0) % tick_size;
    ttt = (tick_size - ttt) % tick_size;
  }

  if(is_split) {
    for(s = sessionlist; s; s = s->next)
      if(s->tickstatus)
	found = 1;

    if(tickcounter_in_splitline(found ? ttt : -1))
      return(1); /* Needs to be updated each second... */
  }

  if(ttt > 10)
    return(ttt-10);
  else if(ttt > 0)
    return(ttt);
  else
    return(tick_size);
}
