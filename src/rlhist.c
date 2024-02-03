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

/* random code to save history, expand !'s, etc */

#include "config.h"
#include "tintin.h"

#include <readline/readline.h>
#include <readline/history.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "include/rl.h"
#include "include/rlhist.h"
#include "include/utils.h"
#include "include/main.h"

void rlhist_show(void)
{
  HIST_ENTRY **a;
  int i;
  char msg[256];

  a = history_list();
  if(!a || !*a) {
    tintin_puts("#No history.", 0);
    return;
  }
  for(i = 0; *a; i++) {
    sprintf(msg, "%2d %s", i, (*a++)->line);
    tintin_puts(msg, 0);
  }
}

void my_add_history(char *s) 
{
    static char lasthist[255];

    if (term_echoing) {
      if (strcmp(s,lasthist)) {
        add_history(s);
        sprintf(lasthist, "%s", s);
      } 
    }
}

char *rlhist_expand(char *line)
{
  char error_msg[256];
  char *expansion;

  if(!*line)
    return(line);

  /* don't try to do history expansion on "say hi there!; bow" */
  if(*line != '!') {
    my_add_history(line);
    return(line);
  }

  /* hack to make "!" work as it used to */
  if(!strcmp(line, "!")) {
    free(line);
    line = mystrdup("!!");
  }

  if(history_expand(line, &expansion) < 0) {
    sprintf(error_msg, "%s", "Expansion error. ");
    if(expansion)
      strcat(error_msg, expansion);
    tintin_puts(error_msg, 0);
    free(line);
    free(expansion);
    return(0);
  }

  free(line);
  my_add_history(expansion);
  return(expansion);
}
