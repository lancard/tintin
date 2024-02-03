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

/* this is the main bunch of code for readline; lots of misc stuff here */

#include "config.h"
#include "tintin.h"

#if defined(HAVE_STRING_H)
#include <string.h>
#elif defined(HAVE_STRINGS_H)
#include <strings.h>
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "include/ansi.h"
#include "include/log.h"
#include "include/main.h"
#include "include/net.h"
#include "include/parse.h"
#include "include/rl.h"
#include "include/rlhist.h"
#include "include/rltab.h"
#include "include/rltick.h"
#include "include/rlvt100.h"
#include "include/scrsize.h"
#include "include/session.h"
#include "include/utils.h"
#include "include/action.h"
#include "include/chat.h"

/* this gets set when user hits control-z, so we don't worry about EINTR */
int ignore_interrupt;

/* should we display the user's commands in the top window, too? */
int am_purist = DEFAULT_PURIST_MODE;

/* the user requested this screen size -- kill autodetection code if set */
int requested_scrsize = -1;

/* essentially the size of the screen; only for split mode */
static int splitline, cols;

/* what column we were at in the top window; only for split mode */
static int topwin_col = 0;

#define ISAPROMPT(x)	(1)

/* some readline stuff */
extern int _rl_last_c_pos;
extern int _rl_eof_char;

static void ctrl_r_redraw()
{
  if(is_split) {
    goto_rowcol(splitline+1, 0);
    erase_toeol();
  }
  else
    printf("\n");

  fflush(stdout);
  _rl_last_c_pos = 0;
  rl_forced_update_display();
}

static void readmud();

/* for use with rl_event_hook */
static void bait(void)
{
  fd_set readfds, excfds;
  struct session *s, *t;
  struct timeval to;
  int rv, maybe_redraw;
  struct chat_data *ch;
  int highest_desc;
  extern int control;
  extern struct chat_data *chat_list;
  extern int enable_chat;

  FD_ZERO(&readfds);
  fflush(stdout);
  while (!FD_ISSET(0, &readfds)) {
    /* added to support zombi mode ---------------- begin
       try to reconnect if there's any disconnected zombi session. */
    int revive_result = 0 ;
    for(s=sessionlist;s;s=s->next)
      if (s->zombistat==ZOMBI_DISCONNECTED)
        revive_result |= revive_zombi(s) ;
    /* added to support zombi mode ---------------- end */

    FD_ZERO(&readfds);
    FD_ZERO(&excfds);
    FD_SET(0, &readfds);	/* stdin */
    FD_SET(control, &readfds);
    for(s = sessionlist; s; s = s->next) {
      if (ZOMBI_IS_ALIVE(s)) /* Make sure for select not to block and listen
			       on disconnected socket descriptors.  - ycjhi */
	FD_SET(s->socket, &readfds);
    }

    for (ch=chat_list; ch; ch=ch->next) {
      FD_SET(ch->fd, &readfds);
      FD_SET(ch->fd, &excfds);
    }
    /* Non-zero revive_result means that there's at least one session
       in zombi mode not connected. To keep trying to reconnect, we
       must do non blocking select() in this case. */
    if (revive_result) {
      checktick() ;   /* checktick() must be called though because */
      to.tv_sec = 0 ; /* we must get the ticker to keep working -- ycjhi */
      to.tv_usec = 0 ;
    } else {
      to.tv_sec = checktick(); /* sec to the next event */
      to.tv_usec = 0;
    }

    highest_desc = (FD_SETSIZE >= (control + 1) ? FD_SETSIZE : control + 1);
    ignore_interrupt = 0;
    rv = select(highest_desc, &readfds, NULL, &excfds, &to);
    /* rv = select(FD_SETSIZE, &readfds, NULL, &excfds, &to); */

    if (!rv)
      continue;	/* timeout */
    else if(rv < 0 && errno == EINTR && ignore_interrupt)
      continue;	/* don't worry, be happy */
    else if(rv < 0)
      syserr("select");

    maybe_redraw = 0;
    if (enable_chat == 1)
      chat_process_connections(&readfds, &excfds);
    for(s = sessionlist; s; s = t) {
      t = s->next;	/* s may be free()d out from under us */
      if(FD_ISSET(s->socket, &readfds)) {
	readmud(s);
	maybe_redraw = 1;
      }
    }
    if(maybe_redraw && redraw && !is_split)
      ctrl_r_redraw();
  }
}

void initrl(void)
{
  rl_readline_name = "tintin++";
  rl_completion_entry_function = (int (*)())rltab_generate;
  using_history();
  stifle_history(HISTORY_SIZE);
  rl_variable_bind("horizontal-scroll-mode", "on");
#ifdef USE_ISO_LATIN_ONE
  rl_variable_bind("meta-flag", "on");
  rl_variable_bind("convert-meta", "off");
  rl_variable_bind("output-meta", "on");
#endif
  rl_bind_key('\022', (Function *) ctrl_r_redraw);	/* control-r */
  rl_bind_key_in_map('\022', (Function *) ctrl_r_redraw, vi_movement_keymap);
  rl_bind_key_in_map('\022', (Function *) ctrl_r_redraw, vi_insertion_keymap);
  rl_event_hook = (int (*)())bait;
}

/* turn on split mode */
void initsplit(void)
{
  int i;

  /* notice the screen size and remember it */
  scrsize(&splitline, &cols);
  if(requested_scrsize > 0)
    splitline = requested_scrsize;
  splitline--;

  is_split = 1;
  topwin_col = 0;
  reset();
  erase_screen();
  scroll_region(1, splitline-1);
  goto_rowcol(splitline, 0);
  for(i = cols; i; i--)
    printf("-");
  goto_rowcol(splitline+1, 0);
  save_pos();
  fflush(stdout);
}

/* ahh, gotta love coding around hardware bugs :-(
 * [this beauty does linewrapping for terminals which
 * are too dumb to do it themselves -- only needed
 * for split mode, i think.]
 * 
 * 'str' better not contain any \n or \r's.
 * if 'isaprompt' is non-zero, then we won't tack
 * a \n\r to the end of 'str', and try to remember that.
 */

static void printline(const char *str, int isaprompt)
{
  int pos = 0, start_pos = 0, k, len = topwin_col;
  char buffer[BUFFER_SIZE];

  if(is_split)
    while(str[pos]) {
      if(!(k = skip_ansi(&str[pos]))) {
	pos++;
	if(++len >= cols) {
	  printf("%.*s\n\r", pos-start_pos, &str[start_pos]);
	  start_pos = pos;
	  len = 0;
	}
      }
      else
	pos += k;

    }

  if(isaprompt && is_split) {
    printf("%s", &str[start_pos]); /* don't append \n\r */
    strip_ansi(&str[start_pos], buffer);
    topwin_col += strlen(buffer) + 1;
  }
  else if (!isaprompt) {
    topwin_col = 0;
    printf("%s\n\r", &str[start_pos]);
  }
  else
    {
      topwin_col = 0;
      printf("%s", &str[start_pos]);
    }
}

void mainloop(void)
{
  char *line;

  initrl();

  for(;;) {
    if(!(line = readline("")))
      continue;
    if(!(line = rlhist_expand(line)))
      continue;

    if(is_split)
      goto_rowcol(splitline-1, topwin_col);

    /* commands will echo to the top screen, except for purists */
    if(is_split) {
      if(!am_purist && term_echoing)
	printline(line, 0);
      else {
	/* should I echo newlines after prompts? */
	/* printline("", 0); */
	;
      }
    }

    activesession = parse_input(line, activesession);

    if(is_split) {
      goto_rowcol(splitline+1, 0);
      erase_toeol();
      fflush(stdout);
    }

    free(line);
  }
}

/* data waiting on this mud session; read & display it; do the dirty work */
/* seriuos bug was found by DasI:                                         */
/* no reaction to actions in incative sessions                            */
/* fixed by DasI                                                          */
/* btw: i don't tested my fix very much, so it's up to you.               */
static void readmud(struct session *s)
{
  char thebuffer[2*BUFFER_SIZE+1], *buf, *line, *next_line;
  /* char mybuf[512]; */
  char linebuf[BUFFER_SIZE], header[BUFFER_SIZE];
  int rv, headerlen;

  /* If not connected, return.  - ycjhi */
  if(!ZOMBI_IS_ALIVE(s))
    return ;

  buf = thebuffer + BUFFER_SIZE;
  rv = read_buffer_mud(buf, s);
  /* sprintf(mybuf, "rv: %d", rv);
     tintin_puts(mybuf, NULL); */ 
  if(!rv) {
    cleanup_session(s);
    if(s == activesession)
      activesession = newactive_session();
    return;
  }
  else if(rv < 0)
    syserr("readmud: read");
  buf[++rv] = '\0';

/* changed by DasI */
  if( s->snoopstatus && (s != activesession))
      sprintf(header, "%s%% ", s->name);
  else
    header[0] = '\0';

  headerlen = strlen(header);

  if(s->old_more_coming) {
    line = s->last_line;
    buf -= strlen(line);
    while(*line)
      *buf++ = *line++;
    buf -= strlen(s->last_line);
    s->last_line[0] = '\0';
  }

  if(strlen(buf)>BUFFER_SIZE)
    syserr("readmud: read one line longer than BUFFERSIZE");

  logit(s, buf);

/* added by DasI */
  if( (s == activesession) || s->snoopstatus )
  {
      if(is_split) {
          save_pos();
          goto_rowcol(splitline-1, topwin_col);
      }
  }

  /* separate into lines and print away */

  for(line = buf; line && *line; line = next_line) 
  {
    if(!(next_line = strchr(line, '\n')) && s->more_coming)
      break;
    if(next_line) {
      *next_line++ = '\0';
      if(*next_line == '\r')			/* ignore \r's */
	next_line++;
    }
    sprintf(linebuf, "%s", line);
    if (is_split && ((s == activesession) || s->snoopstatus )) {
      restore_pos();
      do_one_line(linebuf, s);		/* changes linebuf */
      goto_rowcol(splitline-1, topwin_col);
    }
    else
      do_one_line(linebuf, s);		/* changes linebuf */

/* added by DasI */
    if( (s == activesession) || s->snoopstatus )
    {
        if(strcmp(linebuf, ".")) {
            strcat(header, linebuf);
            if (prompt_on == 0) {
                printline(header, !next_line && ISAPROMPT(header));
            } else
                if (check_status(linebuf, s) == 0)
                    printline(header, !next_line && ISAPROMPT(header));
                else {
                    goto_rowcol(splitline+1, topwin_col);
                    save_pos();
                } 
            header[headerlen] = '\0';
        } 
    }
  }
  
  if(line && *line)
    sprintf(s->last_line, "%s", line);

/* added by DasI */
  if( (s == activesession) || s->snoopstatus )
  {
      if(is_split)
          restore_pos();
      fflush(stdout);
  }

  /* q: do we need to do some sort of redraw here?  i don't think so */
  /* a: no, i do it at the end of mainloop() [for now] */
}


/*
 * output to screen should go through this function
 * the output is NOT checked for actions or anything
 */
void tintin_puts2(const char *cptr, struct session *ses)
{
  if((ses != activesession && ses) || !puts_echoing)
    return;
  if(is_split) {
    save_pos();
    goto_rowcol(splitline-1, topwin_col);
  }
  printline(cptr, 0);
  if(is_split)
    restore_pos();
  fflush(stdout);

  /* q: do we need to do some sort of redraw here?  i don't think so */
  /* a: right now, i think so */
  if(redraw && !is_split)
    ctrl_r_redraw();
}

/*
 * output to screen should go through this function
 * the output IS treated as though it came from the mud
 */
void tintin_puts(const char *cptr, struct session *ses)
{
  /* bug! doesn't do_one_line() sometimes send output to stdout? */
  if(ses) {
    char buf[BUFFER_SIZE];

    sprintf(buf, "%s", cptr);
    do_one_line(buf, ses);
    if(strcmp(buf, "."))
      tintin_puts2(buf, ses);
  }
  else
    tintin_puts2(cptr, ses);
}

/* get a clean screen without the split crap; useful for ^Z, quitting, etc */
void cleanscreen(void)
{
  system("stty echo");	/* a hack, admittedly */

  if(!is_split)
    return;
  scroll_region(1, splitline+1);
  erase_screen();
  reset();
  fflush(stdout);
}

/* undo cleanscreen(); useful after ^Z */
void dirtyscreen(void)
{
  if(is_split)
    initsplit();
}

/* quit tintin++ and print a message */
void quitmsg(const char *m)
{
  struct session *s;
  extern int control;
  char filestring[256];

  s = sessionlist;
  unchat("*");
  close(control);
  while (s) 
  { 
    zombioff_command(s); /* if s is zombi session, cleanup_session won't
                            kill s. turn the zombi mode off. */

    cleanup_session(s);
    s=sessionlist;
  }

  if (save_history == TRUE)
    {
      if (getenv("TINTIN_HISTORY") == NULL)
	sprintf(filestring, "%s/%s", getenv("HOME"), HISTORY_FILE);
      else 
	sprintf(filestring, "%s/%s", getenv("TINTIN_HISTORY"), HISTORY_FILE);
      write_history(filestring);
    }
  cleanscreen();
  if(m)
    printf("%s\n", m);
  printf("Goodbye from tintin++.\n");
  exit(0);
}

/* quit tintin++ fast!  for use with signal() */
RETSIGTYPE myquitsig(int no_care)
{
  quitmsg(NULL);
}

/* Put the tickcounter in the splitline */
int tickcounter_in_splitline(int ttt)
{
  if(is_split) {
    save_pos();
    goto_rowcol(splitline, cols-5);
    if(ttt >= 0)
      printf("%d--", ttt);
    else
      printf("---");
    restore_pos();
    fflush(stdout);
    return(ttt >= 0);
  }

  return(0);
}


/* Put the status line from a mud in the splitline */
int status_in_splitline(const char *arg, struct session *ses)
{
  char buf[BUFFER_SIZE];
 
  if(is_split) {
    save_pos();
    sprintf(buf, "%s", arg);

    if (strlen(buf) < (cols-6)) {
      goto_rowcol(splitline, 1);
      printf("%s", buf);
      restore_pos();
      fflush(stdout);
      return(0);
    }
  }
  return(0);
}
/**********************/
/* the #split command */
/**********************/

/*
 * if the user requests a certain screen size, who are we
 * to autodetect things and ignore his request? :-)
 */
void split_command(const char *arg)
{
  char left[BUFFER_SIZE];

  if(is_split) {
    cleanscreen();
    is_split = 0;
  }

  arg = get_arg_in_braces(arg, left, 0);
  if(!*left)
    requested_scrsize = -1;
  else if((requested_scrsize = atoi(left)) <= 0)
    requested_scrsize = -1;

  initsplit();
}

/************************/
/* the #unsplit command */
/************************/

void unsplit_command(void)
{
  requested_scrsize = -1;
  cleanscreen();
  is_split = 0;
}

/***********************/
/* the #purist command */
/***********************/

void purist_command(void)
{
  tintin_puts("#Ok, purist mode enabled.", 0);
  am_purist = 1;
}

/*************************/
/* the #unpurist command */
/*************************/

void unpurist_command(void)
{
  tintin_puts("#Ok, purist mode disabled.", 0);
  am_purist = 0;
}

/***********************************************/
/* the checking to print prompt on status line */
/***********************************************/
int check_status(const char *buf, struct session *ses)
{

  if (check_one_action(buf, prompt_line, ses) & (is_split)) { 
     status_in_splitline(buf, ses);
     return(1);
  }
  return(0);
}
