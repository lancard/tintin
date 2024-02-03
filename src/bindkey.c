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
/* This file will contain all the functions required for the bindkey command
   in tintin++
   -- DSC 3/8/00
*/

#include "config.h"
#include "tintin.h"

#include <readline/readline.h>
#include <readline/history.h>

#include "include/bindkey.h"
#include "include/rl.h"
#include "include/parse.h"
#include "include/net.h"

void bindkey_command(const char *arg, struct session *ses)
{
  
  if (ses != NULL) {
    /*rl_bind_key('\005', (Function *) do_binding(ses));
    rl_bind_key_in_map('\005', (Function *) do_binding, vi_movement_keymap);
    rl_bind_key_in_map('\005', (Function *) do_binding, vi_insertion_keymap);
    */
    rl_add_defun("testing", (Function *) do_binding(ses), CTRL('1'));

    rl_function_dumper(1);
  }
  else
    tintin_puts("ses is null!", NULL);

}

int do_binding(struct session *ses)
{
  write_line_mud("say test", ses);
  return 0;
}
