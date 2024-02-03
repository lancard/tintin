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
/*
     Modified by Davin Chan for tintin.  I have not been able to contact
     the author of this module.  I will assume this module is free to be
     intergrated into the most recent version of tintin unless I hear
     otherwise from the author (Sean Butler).
     7/19/00

     This module by:
     Sean Butler, sbutler@deveast.com
     January 10, 1998

     chat.c -- An extension to Tintin that allows users to connect their
               clients together in a chat session.  Multiple sessions are
               supported.  The chat protocol used is compatible with
               MudMaster chat and was desgined by the maker of MudMaster.
               All code is however of my creation and therefore direct
               all complaints, comments or praise to me.

     The following new commands have been added to tintin for chat purposes:

     #call
     #chat
     #chatall
     #emote
     #emoteall
     #unchat
     #sendfile
     #filecancel
     #request
     #chattransfer
     #ping


     The scheme used to implement this is a linked list of structures
     containing data about each connection.  This structure was placed
     in tintin.h  

     This is an alpha release and is not yet complete.  There are many
     holes in the parsing of commands.  The most glaring is the failure
     to adhere to the standard tintin syntax.  In this release you may
     not use {} around the arguments to a chat command.  ie.

     #chat {soandso} {Hi there}

     Next release this problem will be fixed.

     The only code that needed to be changed to add this module were these:

     parse.c  -- to add the user commands
     rl.c     -- added a call to chat_process_connections() and added
                 the chat sockets to the call to select() in bait.
     main.c   -- added the call to init_chat()
     misc.c   -- added cleanup call in function end_command.

        Kindly accept my appologies for those of you who are forced to use
     80 column displays, since I regularly go over 80 columns when writing
     this code.
*/
               
#include "config.h"
#include "tintin.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#if defined(HAVE_STRING_H)
#include <string.h>
#elif defined(HAVE_STRINGS_H)
#include <strings.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "include/chat.h"
#include "include/parse.h"
#include "include/rl.h"


/**************
* Global Data *
**************/

int rebind = 1;
int auto_accept = 1;
int my_port = 4050;
char chat_color[SHORT_STRING] = "\x1B[1m\x1B[31m"; /* bright red */
int control;
int num_descs = 0;
int maxdesc = 0;
char my_name[128] = "Tintin";
char my_ip[MAX_STRING_LENGTH] = "";
char dl_path[MAX_STRING_LENGTH] = ".";
struct chat_data *chat_list = NULL;
int outgoing_call_timeout = 4;
int incoming_call_timeout = 4;
int chat_enabled = 1;
extern struct session *activesession;
extern int enable_chat;

/**************************************************************************
 *  SOCKET HANDLING ROUTINES                                              *
 *************************************************************************/

/*
------------------------------------------------------------------------------ 
   chat_init()  -- Get ready to receive connections.
------------------------------------------------------------------------------
*/
int chat_init()
{
    int s;
    int len = MAX_STRING_LENGTH - 1;
    char hostname[MAX_STRING_LENGTH];
    char str[MAX_STRING_LENGTH];
    struct sockaddr_in sa;
    struct hostent *hp=NULL;
    struct sockaddr sock;
    struct sockaddr_in *tmp;
    const char *reuse = "1";
    extern void var_command(char *, struct session *);

    //    read_chat_defaults();

    gethostname(hostname, MAX_STRING_LENGTH -1);
    hp = gethostbyname(hostname);
    if (hp == NULL) {
	perror("chat_init -- gethostbyname");
	exit(1);
    }
    sa.sin_family = hp->h_addrtype;
    sa.sin_port = htons(my_port);
    sa.sin_addr.s_addr = 0;
    /* bzero(&(sa.sin_zero), 8); DSC */
    memset(&(sa.sin_zero), 0, 8);
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("Chat init -- socket");
        exit(1);
    }
    if (rebind)
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reuse, sizeof(reuse));
    if (bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
	printf("# ERROR: Chat port in use cannot initiate chat");
        close(s);
	exit(1);
    }
    nonblock(s);
    listen(s,3);

    if (!*my_ip) {  /* if the user didnt specify his own */
      getsockname(s, &sock, &len);
      tmp = (struct sockaddr_in *)&sock;
      strcpy(my_ip, (char *)inet_ntoa(tmp->sin_addr));
      if (!strcmp(my_ip, "127.0.0.1")) { /* not a real ip */
	if(!get_dynamic_ip(my_ip))
	  strcpy(my_ip, "<unknown>");
      }
    }
    printf("#OK. IP: %s %d\n", my_ip, my_port);
    sprintf(str, "%s %s", "ip", my_ip); 
    var_command(str, activesession);
    control = s;
    maxdesc = control;
    return s;
}

/*
------------------------------------------------------------------------------
  chat_new()  -- Accept an incoming chat connection.
------------------------------------------------------------------------------
*/
int chat_new(int s)
{
    fd_set rds;
    struct chat_data *tmp;
    struct sockaddr_in isa;
    char msg[MAX_STRING_LENGTH];
    int i;
    int t;
    struct timeval to;
    
    to.tv_sec = incoming_call_timeout;
    to.tv_usec = 0;

    i = sizeof(isa);
    getsockname(s, (struct sockaddr *) &isa, &i);

    if ((t = accept(s, (struct sockaddr *) &isa, &i)) < 0){	
	perror("Accept");
	return(-1);
    }
    nonblock(t);
    if (t > maxdesc)
      maxdesc = t;
    num_descs++;
    if((tmp = (struct chat_data *) malloc(sizeof(struct chat_data))) == NULL) {
	perror("chat_new -- malloc");
        exit(1);
    }
    *(tmp->chatname) = '\0';
    *(tmp->ip) = '\0';
    tmp->port = 0;
    tmp->fd = t;
    tmp->flags = 0;
    *(tmp->version) = '\0';
    *(tmp->readq) = '\0';
    *(tmp->writeq) = '\0';

    FD_ZERO(&rds);
    FD_SET(tmp->fd, &rds);
    select(FD_SETSIZE, &rds, NULL, NULL, &to);
    if (!FD_ISSET(tmp->fd, &rds)) {
      chat_puts("<CHAT> Connection attempt timed out.");
      close(tmp->fd);
      free(tmp);
      return(-1); /* DSC: return -1 because we failed.  */
    }      
    process_input(tmp);
    sscanf(tmp->readq, "CHAT:%s\n%s", tmp->chatname, tmp->ip);
    if (strlen(tmp->ip) > 4) {
      tmp->port = atoi(tmp->ip + (strlen(tmp->ip) - 4));
      *((tmp->ip) + strlen(tmp->ip) - 4) = '\0';
    }
    *(tmp->readq) = '\0';
    sprintf(tmp->writeq, "YES:%s\n", my_name);
    process_output(tmp);
    tmp->next = chat_list;
    chat_list = tmp;
    sprintf(msg, "<CHAT> Connection from %s", tmp->chatname);
    chat_puts(msg);
    sprintf(msg, "%c%s%c", CHAT_VERSION, VERSION_NUM, CHAT_END_OF_COMMAND);
    strcat(tmp->writeq, msg);
    return(t);
}

/*
------------------------------------------------------------------------------
  chat_call() -- Places a call to another chat client.
------------------------------------------------------------------------------
*/
void chat_call(const char *ip)
{
  int port = 4050;
  int n;
  int sockfd;
  char s[MAX_STRING_LENGTH];
  char pip[SHORT_STRING];
  char pport[SHORT_STRING];
  struct sockaddr_in dest_addr;
  struct chat_data *tmp;
  int counter = 0;
  struct timeval to;
  fd_set rds;
  fd_set wds;

  sprintf(s, "<CHAT> Attempting to call %s ...", ip);
  chat_puts(s);

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    chat_puts("<CHAT> Couldn't create a socket.");
    return;
  }

  ip = get_arg_in_braces(ip, pip, 0);
  ip = get_arg_in_braces(ip, pport, 0);

  if (*pport)
    port = atoi(pport);
  nonblock(sockfd);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);
  dest_addr.sin_addr.s_addr = inet_addr(pip);
  /* bzero(&(dest_addr.sin_zero), 8); DSC */
  memset(&(dest_addr.sin_zero), 0, 8);

  to.tv_sec = outgoing_call_timeout;
  to.tv_usec = 0;
  if (connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr)) != 0) {
    chat_puts("<CHAT> Press ENTER to abort call.");
    FD_ZERO(&rds);
    FD_ZERO(&wds);
    FD_SET(0, &rds);	  /* stdin */
    FD_SET(sockfd, &wds); /* mother chat desc */
    select(sockfd+1, &rds, &wds, NULL, &to);
  }
  if (FD_ISSET(0, &rds)) {
    chat_puts("<CHAT> call aborted.");
    return;
  } 
  if (!FD_ISSET(sockfd, &wds)) {
    chat_puts("<CHAT> Connection timed out.");
    return;
  }
  if((tmp = (struct chat_data *) malloc(sizeof(struct chat_data))) == NULL) {
    perror("chat_call -- malloc");
      exit(1);
  }
  chat_puts("<CHAT> Socket connected, negotiating protocol...");
  *(tmp->chatname) = '\0';
  strcpy(tmp->ip, pip);
  tmp->port = port;
  tmp->fd = sockfd;
  tmp->flags = 0;
  *(tmp->version) = '\0';
  *(tmp->readq) = '\0';
  *(tmp->writeq) = '\0';

  sprintf(s, "CHAT:%s\n%s%-5u", my_name, my_ip, my_port);
  *(tmp->writeq) = 0;
  strcat(tmp->writeq, s);
  process_output(tmp);
  counter = 0;
  FD_ZERO(&rds);
  FD_SET(tmp->fd, &rds);
  to.tv_sec = outgoing_call_timeout;
  to.tv_usec = 0;
  if(select(FD_SETSIZE, &rds, NULL, NULL, &to) == -1) {
    close_chat(tmp);
    return;
  }
  n = process_input(tmp);
  if (n == -1) {
    close_chat(tmp);
    return;
  } 
  sscanf(tmp->readq, "YES:%s\n", tmp->chatname);
  sprintf(s, "<CHAT> Connection made to %s", tmp->chatname);
  chat_puts(s); 
  *(tmp->readq) = '\0';   
  if (sockfd > maxdesc)
    maxdesc = sockfd;
  num_descs++ ;
  tmp->next = chat_list;
  chat_list = tmp;
}


/*
------------------------------------------------------------------------------
  close_chat() -- Clean up and close a chat conneciton.
------------------------------------------------------------------------------
*/
void close_chat(struct chat_data *ch)
{
    struct chat_data *tmp;
    char s[MAX_STRING_LENGTH];

    close(ch->fd);
    num_descs--;
    if (ch->fd == maxdesc)
      maxdesc--;
    if (chat_list == ch) {
      sprintf(s, "<CHAT> Closing connection to %s.", ch->chatname);
      chat_puts(s);
      chat_list = chat_list->next;
      free(ch);
    } else {
      for (tmp = chat_list; tmp && tmp->next; tmp = tmp->next) {
	if (tmp->next == ch) {
	  sprintf(s, "<CHAT> Closing connection to %s.", ch->chatname);
	  chat_puts(s);
	  tmp->next = tmp->next->next;
	  free(ch);
	  break;
	}
      }
    }    
}

/*
------------------------------------------------------------------------------
  nonblock() -- Set a socket to nonblocking.
------------------------------------------------------------------------------
*/
void nonblock(int s)
{
  int flags;
  if ((flags = fcntl(s, F_GETFL, 0)) == -1)
    exit(1);
  if (fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("Noblock");
    exit(1);
 }
}

/*
------------------------------------------------------------------------------
  block() -- Set a socket to blocking.
------------------------------------------------------------------------------
*/

void block(int s)
{
  int flags;
  if ((flags = fcntl(s, F_GETFL, 0)) == -1)
    exit(1);
  if (fcntl(s, F_SETFL, flags & ~O_NONBLOCK) == -1) {
    perror("Noblock");
    exit(1);
  }
}  

/*
------------------------------------------------------------------------------
  chat_process_connections() -- Check for incoming calls and poll for input on 
                                connected sockets.
------------------------------------------------------------------------------
*/
void chat_process_connections(fd_set *input_set, fd_set *exc_set)
{
    struct chat_data *ch = NULL;
    struct chat_data *next_ch;

    /* accept incoming connections */
    if (FD_ISSET(control, input_set))
	if (chat_new(control) < 0)
	    perror("chat_process_connections -- chat_new");

    /* read from sockets */
    for (ch = chat_list; ch; ch = next_ch) { 
	next_ch = ch->next;
	if (FD_ISSET(ch->fd, input_set))
	  if (process_input(ch) < 0)
	    close_chat(ch);
    }
}

/*
------------------------------------------------------------------------------
  process_output() -- Send all queued data to a chat connection.  
------------------------------------------------------------------------------
*/
void process_output(struct chat_data *ch)
{
  char s[MAX_STRING_LENGTH];

  if (send(ch->fd, ch->writeq, strlen(ch->writeq), 0) < 0 ) {
    sprintf(s, "<CHAT> %s has lost connection.", ch->chatname);
    chat_puts(s);
    close_chat(ch);
  }
  *(ch->writeq) = 0;
}

/*
------------------------------------------------------------------------------
  process_input() -- Read and store for parsing all input on a socket.
                     Then call get_chat_commands() to parse out the commands.
------------------------------------------------------------------------------
*/
int process_input(struct chat_data *ch)
{
  int nBytes;
  int nBuflen = MAX_STRING_LENGTH;
  char *pBuf = ch->readq;

  nBytes = recv(ch->fd, pBuf, nBuflen, 0);
  while(nBytes > 0) {
    pBuf += nBytes;
    nBuflen -= nBytes;
    nBytes = recv(ch->fd, pBuf, nBuflen, 0);
  }
  *pBuf = '\0';
  if ((nBytes  < 0) && (errno == EWOULDBLOCK) && (strlen(ch->readq) == 0)) {
    chat_puts("would block");
    return -1;
  }
  /* deal with connections individually */
  if (!strncmp(ch->readq, "CHAT:", 5))
    return 1;
  if (!strncmp(ch->readq, "YES:", 4))
    return 1;
  if (!strncmp(ch->readq, "NO:", 3))
    return -1;
  get_chat_commands(ch);
  return 0; /* no error, return 0 */
}

/*
------------------------------------------------------------------------------
  get_chat_commands() -- Parse the input queue for commands and execute the
                         appropriate function.
------------------------------------------------------------------------------
*/
int get_chat_commands(struct chat_data *ch)
{
  unsigned char *s = ch->readq;
  unsigned char *t = ch->readq;
  char txt[MAX_STRING_LENGTH];
  char tmp[MAX_STRING_LENGTH];

  if (*t == CHAT_FILE_BLOCK) { /* have to deal with these first */
    recv_block((t + 1), ch);
    return 1;
  }
  while (*t) {
    while(*t && (*t != CHAT_END_OF_COMMAND))
       t++;
    if (!*t || (t == s)) {
      *(ch->readq) = '\0';  /* ignore in correctly formatted commands */
      return -1;
    }
    *txt = '\0';
    strncpy (txt, s + 1, t - s - 1);
    *(txt + (t - s - 1)) = '\0';
    switch (*s) {
    case CHAT_NAME_CHANGE:
      sprintf(tmp, "\n<CHAT> %s is now %s\n", ch->chatname, txt);
      strcpy(ch->chatname, txt);
      chat_puts(tmp);
      break;
    case CHAT_REQUEST_CONNECTIONS:
      request_response(ch);
      break;
    case CHAT_CONNECTION_LIST:
      parse_connections(txt);
      break;
    case CHAT_TEXT_EVERYBODY:
      chat_puts(txt);
      break;
    case CHAT_TEXT_PERSONAL:
      chat_puts(txt);
      break;
    case CHAT_TEXT_GROUP:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_MESSAGE:
      chat_puts(txt);
      break;
    case CHAT_DO_NOT_DISTURB:
      break;
    case CHAT_SEND_ACTION:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_ALIAS:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_VARIABLE:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_EVENT:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_GAG:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_HIGHLIGHT:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_LIST:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_ARRAY:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_SEND_BARITEM:
      chat_message("\n<CHAT> Tintin++ does not yet support this.\n", ch);
      break;
    case CHAT_VERSION:
      strcpy(ch->version, txt);
      sprintf(tmp, "%c%s%c", CHAT_VERSION, VERSION_NUM, CHAT_END_OF_COMMAND);
      strcat(ch->writeq, tmp);
      break;
    case CHAT_FILE_START:
      recv_file(txt, ch);
      break;
    case CHAT_FILE_DENY:
      file_denied(ch, txt);
      break;
    case CHAT_FILE_BLOCK_REQUEST:
      if (!(*(ch->filename))) {
	tintin_puts2("Lost file name", NULL);
	sprintf(tmp, "%c%c", CHAT_FILE_END, CHAT_END_OF_COMMAND);
        strcat(ch->writeq, tmp);
	process_output(ch);
      } else {
	send_block(ch);
      }
      break;
    case CHAT_FILE_BLOCK:
      recv_block(txt, ch);
      break;
    case CHAT_FILE_END:
      chat_puts("<CHAT> File transfer completion acknowledged.");
      break;
    case CHAT_FILE_CANCEL:
      chat_puts("<CHAT> File cancel request received.");
      if (*ch->filename) {
	*(ch->filename) = 0;
	fclose(ch->xfer_fd);
	ch->blocks_recvd = 0;
	ch->last_block = 0;
	ch->filesize = 0;
      }
      break;
    case CHAT_PING_REQUEST:
      sprintf(tmp, "%c%s%c", CHAT_PING_RESPONSE, txt, CHAT_END_OF_COMMAND);
      strcat(ch->writeq, tmp);
      break;
    case CHAT_PING_RESPONSE:
      ping_response(ch, txt);
      break;
    default:
      break;
    }
    s = t + 1;
    t = s;
  }
  return 0;
}

/**************************************************************************
 *  CHAT COMMUNICATION ROUTINES                                           *
 *************************************************************************/

/*
------------------------------------------------------------------------------
  chat_all() -- Send a text message to all connections of the form:
                Radric chats to everybody, 'Hi there.'
------------------------------------------------------------------------------
*/
void chat_all(const char *txt)
{
  struct chat_data *ch;
  char s[MAX_STRING_LENGTH];
  char left[MAX_STRING_LENGTH];

  txt = get_arg_in_braces(txt, left, 1);

  sprintf(s, "You chat to everyone, '%s'", left);
  chat_puts(s);
  for (ch = chat_list; ch; ch = ch->next) {
    sprintf(s, "%c\n%s chats to everbody, '%s'\n%c", 
            CHAT_TEXT_EVERYBODY, my_name, left, CHAT_END_OF_COMMAND);
    strcat(ch->writeq, s);
    process_output(ch);
  }
  *s = '\0';
}

/*
------------------------------------------------------------------------------
  emote_all() -- Send a text message to all connections of the form:
                 Radric thinks Tintin rocks!
------------------------------------------------------------------------------
*/
void emote_all(const char *txt)
{
  struct chat_data *ch;
  char s[MAX_STRING_LENGTH];
  char left[MAX_STRING_LENGTH];

  txt = get_arg_in_braces(txt, left, 1);

  sprintf(s, "You emote to everyone: %s %s", my_name, left);
  chat_puts(s);
  for (ch = chat_list; ch; ch = ch->next) {
    sprintf(s, "%c\n%s %s\n%c", 
            CHAT_TEXT_EVERYBODY, my_name, left, CHAT_END_OF_COMMAND);
    strcat(ch->writeq, s);
    process_output(ch);
  }
  *s = '\0';
}

/*
------------------------------------------------------------------------------
  emote() -- Send a text message to just one connection:
             Radric hugs you.
------------------------------------------------------------------------------
*/
void emote(const char *txt)
{
  struct chat_data *ch;
  char s[MAX_STRING_LENGTH];
  char left[SHORT_STRING];
  char right[MAX_STRING_LENGTH];

  txt = get_arg_in_braces(txt, left, 0);
  txt = get_arg_in_braces(txt, right, 1);
  if (!*left) 
    return;
  if ((ch = get_ch_by_name(left)) != NULL) {
      sprintf(s, "You emote to %s: %s %s", ch->chatname, my_name, right);
      chat_puts(s);
      sprintf(s, "%c\n%s %s\n%c", CHAT_TEXT_PERSONAL, my_name, right, CHAT_END_OF_COMMAND);
      strcat(ch->writeq, s);
      process_output(ch);
  }
  *s = '\0';
}

/*
------------------------------------------------------------------------------
  chat() -- Dual purpose:
            1. Send a text message to a single connection:
                   Radric chats to you, 'Hi there.'
            2. With no argument it prints the list of connections.
------------------------------------------------------------------------------
*/
void chat(const char *txt)
{
  struct chat_data *ch;
  char s[MAX_STRING_LENGTH];
  char left[SHORT_STRING];
  char right[MAX_STRING_LENGTH];

  txt = get_arg_in_braces(txt, left, 0);
  txt = get_arg_in_braces(txt, right, 1);
  
  if (!*left) {
    chat_puts("OPEN CHAT CONNECTIONS:");
    for (ch = chat_list; ch; ch = ch->next) {
      sprintf(s, "[%-15s] -- [%-11s] [%-5u] [%s]", ch->chatname, ch->ip, ch->port, ch->version);
      chat_puts(s);
    }
    return;
  }
  if (!*right)
    return;
  if ((ch = get_ch_by_name(left)) != NULL) {
    sprintf(s, "You chat to %s, '%s'", ch->chatname, right);
    chat_puts(s);
    *s = '\0';
    sprintf(s, "%c\n%s chats to you, '%s'\n%c", CHAT_TEXT_PERSONAL, my_name, right, CHAT_END_OF_COMMAND);
    strcat(ch->writeq, s);
    process_output(ch);
  }
  *s = '\0';
}

/*
------------------------------------------------------------------------------
  chat_message -- Send a text message to inform a connection of status.
                  (Can have any format, the user shouldn't call this.)
------------------------------------------------------------------------------
*/
void chat_message(char *msg, struct chat_data *ch)
{
  char t[MAX_STRING_LENGTH];

  sprintf(t, "%c%s%c", CHAT_MESSAGE, msg, CHAT_END_OF_COMMAND);
  strcat(ch->writeq, t);
  process_output(ch);
}


/**************************************************************************
 * USER UTILITY COMMANDS                                                  *
 *************************************************************************/


/*
------------------------------------------------------------------------------
  unchat() -- User command to clean up and close a chat conneciton.
------------------------------------------------------------------------------
*/
void unchat(const char *name)
{
  struct chat_data *ch;
  struct chat_data *next_ch;
  char left[SHORT_STRING];

  name = get_arg_in_braces(name, left, 1);
  if (!*left)
    return;
  if (*left != '*') {
    ch = get_ch_by_name(left);
    if (ch)
      close_chat(ch);
  } else {
    for (ch = chat_list; ch; ch = next_ch) {
      next_ch = ch->next;
      close_chat(ch);
    }
  }
}

/*
------------------------------------------------------------------------------
  chatname() -- Changes your name to the new one specified.
------------------------------------------------------------------------------
*/
void chatname(const char *name)
{
  char s[SHORT_STRING];
  struct chat_data *ch;

  name = get_arg_in_braces(name, my_name, 1);
  sprintf(s, "%c%s%c", CHAT_NAME_CHANGE, my_name, CHAT_END_OF_COMMAND);
  for(ch = chat_list; ch; ch = ch->next) {
    strcat(ch->writeq, s);
    process_output(ch);
  }
  sprintf(s, "<CHAT> Name changed to %s", my_name);
  chat_puts(s);
}


/*
------------------------------------------------------------------------------
  chat_ping() -- Send a chat message containing timing info to measure
                 network latency.
------------------------------------------------------------------------------
*/
int chat_ping(const char *arg)
{
  struct timeval t;
  struct timezone z = {0, 0};
  char s[SHORT_STRING];
  char sname[SHORT_STRING];
  struct chat_data *ch;

  arg = get_arg_in_braces(arg, sname, 1);
  if (!*sname)
    return 0;
  if (!(ch = get_ch_by_name(sname))) {
    sprintf(s, "# No person connected the name of <%s>", sname);
    chat_puts(s);
    return -1;
  }

  gettimeofday(&t, &z);

  sprintf(s, "%csecs: %d usecs: %d%c", 
	  CHAT_PING_REQUEST, (int)t.tv_sec, (int)t.tv_usec, CHAT_END_OF_COMMAND);

  strcat(ch->writeq, s);
  sprintf(s, "# Ping request sent to %s.", ch->chatname);
  chat_puts(s);
  process_output(ch);
  return 1;
}

/*
------------------------------------------------------------------------------
  ping_response() -- Measure the amount of time it took for a message to go 
                     from you to your chat connection, and back again.  Useful
                     for determining if a person is lagged.
------------------------------------------------------------------------------
*/
int ping_response(struct chat_data *ch, char *time)
{
  struct timeval now;
  struct timezone z = {0, 0};
  char s[MAX_STRING_LENGTH];
  long secs = 0;
  long usecs = 0;

  gettimeofday(&now, &z);

  sscanf(time, "secs: %ld usecs: %ld", &secs, &usecs);

  secs = now.tv_sec - secs;
  usecs = now.tv_usec - usecs;
  if (usecs < 0) {
    secs  -= 1;
    usecs += 1000000;
  }
  sprintf(s, "# Ping Response time for %s: %d.%2d", 
	  ch->chatname, (int)secs, (int)usecs / 10000);
  chat_puts(s);
  return 1;
}

/*
------------------------------------------------------------------------------
  request_conns() -- User command to request a list of connections from
                     a chat connection.
------------------------------------------------------------------------------
*/
int request_conns(const char *arg)
{
  char s[MAX_STRING_LENGTH];
  char left[MAX_STRING_LENGTH];
  struct chat_data *ch;

  arg = get_arg_in_braces(arg, left, 1);
  if (!*left)
    return 0;
  if (!(ch = get_ch_by_name(left)))
    return -1;
  sprintf(s, "%c%c", CHAT_REQUEST_CONNECTIONS, CHAT_END_OF_COMMAND);
  strcat(ch->writeq, s);
  sprintf(s, "<CHAT> You request %s's public connections.", ch->chatname);
  chat_puts(s);
  return 0;
}
/*
------------------------------------------------------------------------------
  request_response() -- Responds to a request for connections.
                        Generates a list of IP addresses and ports of those
                        people connected, and sends them in a comma delimited
                        list to the requester.
------------------------------------------------------------------------------
*/
int request_response(struct chat_data *req)
{
  struct chat_data *ch;
  char response[MAX_STRING_LENGTH] = "";
  char s[MAX_STRING_LENGTH];
  sprintf(s, "<CHAT> %s has requested your public connections.", req->chatname);
  chat_puts(s);
  for (ch = chat_list; ch; ch = ch->next) {
    if (ch != req && !(ch->flags & CHAT_PRIVATE)) {
      sprintf(s, "%s,%-5u", ch->ip, ch->port);
      if(*response) strcat(response, ",");
      strcat(response, s);
    }
  }
  *s = '\0';
  sprintf(s, "%c%s%c", CHAT_CONNECTION_LIST, response, CHAT_END_OF_COMMAND);
  strcat(req->writeq, s);
  process_output(req);
  return 0;
}
      
/*
------------------------------------------------------------------------------
  parse_connections() -- After receiving a a response from a request for
                         connections, this function will parse out and call
                         each of the IP addressese returned.
------------------------------------------------------------------------------
*/
int parse_connections(char *txt)
{
   char *comma = txt;
   char *start = txt;
   char address[MAX_STRING_LENGTH] = "";

   int connected = 0;
   struct chat_data *ch;
   while (comma && *comma) {
     if (!(comma = strchr(start, ',')))
       break;
     strncat(address, start, (comma - start));
     *(address + (comma - start)) = '\0';
     for (ch = chat_list; ch; ch = ch->next) {
       if (!strcmp(address, ch->ip)) {
	 connected = 1;
	 break;
       }
     }
     if (!connected)
       chat_call(address);
     start = comma + 1;         
     comma = strchr(start, ',');   /* skip port for now */
     if (comma)
       start = comma + 1;
     connected = 0;
     *address = '\0';
   }
   return 1;   
}

/*
------------------------------------------------------------------------------
  display_cinfo() -- Show the user the current chat configuration.
------------------------------------------------------------------------------
*/
void display_cinfo()
{
  char s[MAX_STRING_LENGTH];

  sprintf(s, "Name                 : %s", my_name);
  chat_puts(s);
  sprintf(s, "IP Address           : %s", my_ip);
  chat_puts(s);
  sprintf(s, "Chat Port            : %d", my_port);
  chat_puts(s);
  sprintf(s, "Download Dir         : %s", dl_path);
  chat_puts(s);
  sprintf(s, "Auto Accept          : %s", (auto_accept ? "ON" : "OFF"));
  chat_puts(s);
  sprintf(s, "Incoming call timeout: %d sec", incoming_call_timeout);
  chat_puts(s);
  sprintf(s, "Outgoing call timeout: %d sec", outgoing_call_timeout);
  chat_puts(s);
  sprintf(s, "Rebind if port used? : %s", (rebind ? "YES" : "NO"));
  chat_puts(s);
}

void chatpriv(const char *name)
{
  char s[MAX_STRING_LENGTH];
  char left[MAX_STRING_LENGTH];
  struct chat_data *ch;

  name = get_arg_in_braces(name, left, 1);
  if (!*left)
    return;
  if ((ch = get_ch_by_name(left)) == NULL)
    return;
  /* toggle the chat private flag for ch */
  ch->flags ^= CHAT_PRIVATE;

  if (ch->flags & CHAT_PRIVATE) {
    sprintf(s, "<CHAT> You have made %s a private connection.", ch->chatname);
    chat_puts(s);
    sprintf(s, "\n<CHAT> %s has marked your connection private.\n", my_name);
    chat_message(s, ch);
  } else {
    sprintf(s, "<CHAT> You have made %s a public connection.", ch->chatname);
    chat_puts(s);
    sprintf(s, "\n<CHAT> %s has marked your connection public.\n", my_name);
    chat_message(s, ch);
  } 
}    


/**************************************************************************
 * FILE TRANSFER FUNCTIONS                                                *
 *************************************************************************/

/*
------------------------------------------------------------------------------
  tt_send_file() -- Send the initial info about the transfer to take place.
                 Can only send one file at a time to a chat connection.
                 Cannot both send and receive on the same connection.
                 One file to or from each chat connection is ok however.

Renamed from send_file to tt_send_file as send_file is used on AIX -- DSC
------------------------------------------------------------------------------
*/
int tt_send_file(const char *arg)
{
   char left[SHORT_STRING];
   char right[MAX_STRING_LENGTH];
   struct chat_data *ch;
   char t[MAX_STRING_LENGTH];
   struct timeval now;
   struct timezone z = {0, 0};

   gettimeofday(&now, &z);

   /* Determine the chat connection refered to */
   arg = get_arg_in_braces(arg, left, 0);
   arg = get_arg_in_braces(arg, right, 0);

   if (!*left) {
      chat_puts("<CHAT> USAGE: #sendfile <person> <filename>");
      return 0;
   }
   if((ch = get_ch_by_name(left)) == NULL) {
       chat_puts("<CHAT> No one by that name is connected to you.");
       return 0;
   }
   if (*(ch->filename)) {
       chat_puts("<CHAT> ERROR: You already have a file transfer in progress with that person.");
       return 0;
   }

   ch->blocks_recvd = 0;
   ch->last_block = 0;
   if (!*right) {
      chat_puts("<CHAT> USAGE: #sendfile <person> <filename>");
      return 0;
   }
   strcpy(ch->filename, right);

   /* Open file for read */
   if ((ch->xfer_fd = fopen(ch->filename, "r")) == NULL) {
     chat_puts("<CHAT> ERROR: No such file.");
     return 0;
   }
   /* determine its size */
   if ((ch->filesize = get_file_size(ch->filename)) == 0) {
     chat_puts("<CHAT> Cannot send an empty file.");
     fclose(ch->xfer_fd);
     return 0;
   }
     ch->last_block = ((ch->filesize / BLOCK_SIZE) + ((ch->filesize % BLOCK_SIZE) ? 1 : 0));
   /* Strip the dir info from the file name */
   if (!strip_path_info(ch->filename)) {
     chat_puts("<CHAT> Must be a file, directories not accepted.");
     fclose(ch->xfer_fd);
     return 0;
   }
   ch->transfer_start_time = now.tv_sec;

   /* send notification about the pending xfer */
   sprintf (t, "%c%s,%d%c", CHAT_FILE_START, ch->filename, ch->filesize, CHAT_END_OF_COMMAND);
   strcat(ch->writeq, t);
   process_output(ch);
   sprintf(t, "<CHAT> Sending file to: %s, File: %s, Size: %d", ch->chatname, ch->filename, ch->filesize);
   chat_puts(t);
   return 1;
}

/*
------------------------------------------------------------------------------
  recv_file() -- Prepare to receive a file from sender and then send the
                 request for the first block of data.
------------------------------------------------------------------------------
*/
int recv_file(char *arg, struct chat_data *ch)
{
  char t[MAX_STRING_LENGTH];
  char path[MAX_STRING_LENGTH];
  char *s;
  struct timeval now;
  struct timezone z = {0, 0};

  gettimeofday(&now, &z);

  if (*(ch->filename)) {
    deny_file(ch, "\n<CHAT> There is a transfer already in progress.\n");
    return 0;
  }
  *(ch->filename) = 0;
  ch->blocks_recvd = 0;
  ch->last_block = 0;
  ch->filesize = 0;
  /* Is this xfer allowed */
  if (!( (ch->flags) | CHAT_XFER_IALLOW)) {
    deny_file(ch, "\n<CHAT> That person is not accepting file transfers from you.\n");
    return 0;
  }
  /* Parse the args */
  if ((s = strchr(arg, ',')) == NULL) {
    deny_file(ch, "\n<CHAT> File protocol error. (no file size was transmitted)\n");
    return 0;
  }
  strncpy(ch->filename, arg, s - arg);
  if (!strip_path_info(ch->filename)) {
    deny_file(ch, "\n<CHAT> Filename sent with directory info. (rejected)\n");
    *(ch->filename) = 0;
    return 0;
  }
  if (!*(s+1)) {
    deny_file(ch, "\n<CHAT> File protocol error. (no file size was transmitted)\n");
    *(ch->filename) = 0;
    return 0;
  }
  if (!(ch->filesize = atoi(s+1))) {
    deny_file(ch, "\n<CHAT> File protocol error. (no file size was transmitted)\n");
    *(ch->filename) = 0;
    return 0;
  }

  /* determine how many blocks to expect */
  ch->last_block = ((ch->filesize / BLOCK_SIZE) + ((ch->filesize % BLOCK_SIZE) ? 1 : 0));
  
  /* open file for write */
  strcpy(path, dl_path);
  strcat(path, ch->filename);
  if ((ch->xfer_fd = fopen(path, "w")) == NULL) {
    deny_file(ch, "\n<CHAT> Could not create that file on receiver's end.\n");
    *(ch->filename) = 0;
    return 0;
  }

  ch->transfer_start_time = now.tv_sec;

  /* ask for a BLOCK_SIZE byte block */
  req_next_block(ch);
  sprintf(t, "<CHAT> File transfer from %s, file: %s, size: %d", ch->chatname, ch->filename, ch->filesize);
  chat_puts(t);
  return 1;
}

/*
------------------------------------------------------------------------------
  send_block() -- Send BLOCK_SIZE bytes of data to ch in one block. 
------------------------------------------------------------------------------
*/
int send_block(struct chat_data *ch)
{
  char s[MAX_STRING_LENGTH];
  int i;
  int c;
  

  /* A null filename indicates that the file is closed or never opened. */

  if (!ch->filename)
    return 0;
  *(ch->writeq) = CHAT_FILE_BLOCK;
  /* Read until we get BLOCK_SIZE bytes, or EOF */
  for (i = 0; i < BLOCK_SIZE; i++) {
    if( (c = fgetc(ch->xfer_fd)) == EOF)
      break;
    *((ch->writeq) + i + 1) = (unsigned char)c;
  }
  *((ch->writeq) + 501) = CHAT_END_OF_COMMAND;
  file_block_send(ch);
  ch->blocks_recvd++;
  *(ch->writeq) = 0;
  /* if at the end, close the file and notify user */
  if (i < BLOCK_SIZE) {
    fclose(ch->xfer_fd);
    sprintf(s, "<CHAT> File transfer: %s, to %s completed.", ch->filename, ch->chatname);
    chat_puts(s);
    *(ch->filename) = 0;
  }
  return 1;
}

/*
------------------------------------------------------------------------------
 recv_block() -- Receive BLOCK_SIZE bytes of data for file sent by ch.
------------------------------------------------------------------------------
*/
int recv_block(unsigned char *s, struct chat_data *ch)
{
    int len;

    if (!ch->filename)
      return 0;

    /* keep track of blocks recvd so we know when we are done */
    ch->blocks_recvd++;
    
    /* compare the number of blocks received to the number needed */
    if (ch->blocks_recvd == ch->last_block) {
      len = ch->filesize % BLOCK_SIZE;
      fwrite(s, 1, len, ch->xfer_fd);
      fclose(ch->xfer_fd);
      *(ch->filename) = 0;
      chat_puts("<CHAT> File transfer completed");
    }
    /* else write the block and reqest the next one */
    fwrite(s, 1, BLOCK_SIZE, ch->xfer_fd);
    req_next_block(ch);
    return 1;
}

/*
------------------------------------------------------------------------------
  req_next_block() -- Ask the sender to send the next block of data.
------------------------------------------------------------------------------
*/
void req_next_block(struct chat_data *ch)
{
  char s[MAX_STRING_LENGTH];
  
  /* Send request for next block */
  sprintf(s, "%c%c", CHAT_FILE_BLOCK_REQUEST, CHAT_END_OF_COMMAND);
  strcat(ch->writeq, s);
  process_output(ch);
}

/*
------------------------------------------------------------------------------
  chat_xfer_toggle() -- Toggles whether or not a particular connection can
                        send you files.
------------------------------------------------------------------------------
*/
/* Enable transfers for a chat connection */
int chat_xfer_toggle(const char *arg)
{
  char name[MAX_STRING_LENGTH];
  struct chat_data *ch;
  char s[MAX_STRING_LENGTH];

  arg = get_arg_in_braces(arg, name, 1);
  if (!*name) return 0;
  if (!(ch = get_ch_by_name(name)))
    return 0;
  /* toggle the chat transfer flag for ch */
  ch->flags ^= CHAT_XFER_IALLOW;

  if (ch->flags & CHAT_XFER_IALLOW) {
    sprintf(s, "<CHAT> You have enabled file transfers from %s.", ch->chatname);
    chat_puts(s);
    sprintf(s, "\n<CHAT> %s is now accepting file transfers from you.\n", my_name);
    chat_message(s, ch);
  } else {
    sprintf(s, "<CHAT> You have disabled file transfers from %s.", ch->chatname);
    chat_puts(s);
    sprintf(s, "\n<CHAT> %s is no longer accepting file transfers from you.\n", my_name);
    chat_message(s, ch);
  }
  return 1;
}

/*
------------------------------------------------------------------------------
  deny_file() -- Inform the sender that you will not accept a transfer.
------------------------------------------------------------------------------
*/
void deny_file(struct chat_data *ch, char *arg)
{
  char s[MAX_STRING_LENGTH];
  
  sprintf(s, "%c%s%c", CHAT_FILE_DENY, arg, CHAT_END_OF_COMMAND);
  strcat(ch->writeq, s);
  process_output(ch);
}

/*
------------------------------------------------------------------------------
  file_denied() -- After a file deny message is received, clean up the
                   file data.
------------------------------------------------------------------------------
*/
/* file was denied so clean up the ch's struct */
void file_denied(struct chat_data *ch, char *txt)
{
  if (!ch->filename)
    return;
  tintin_puts2(txt, NULL);
  fclose(ch->xfer_fd);
  *(ch->filename) = 0;
  ch->blocks_recvd = 0;
  ch->last_block = 0;
}

/*
------------------------------------------------------------------------------
  file_block_send() -- Send a 500 byte block to ch.
------------------------------------------------------------------------------
*/
int file_block_send(struct chat_data *ch)
{
  int nBytes = 0;
  int nTotal = 0;

  while ((nTotal < 502) && (nBytes > -1)) {
    nBytes = send(ch->fd, (ch->writeq) + nTotal, 502 - nTotal, 0);
    nTotal += nBytes;
  }
  if (nBytes < 0)
    return 0;
  return 1;
}

/*
------------------------------------------------------------------------------
  file_cancel() -- Cancel a file transfer in progress.
------------------------------------------------------------------------------
*/
void file_cancel(const char *name)
{
  struct chat_data *ch;
  char s[SHORT_STRING];
  char left[SHORT_STRING];

  name = get_arg_in_braces(name, left, 1);
  if (!*left)
    return;
  if ((ch = get_ch_by_name(left)) == NULL)
    return;
  if (!*(ch->filename))
    return;
  fclose(ch->xfer_fd);
  *(ch->filename) = 0;
  ch->last_block = 0;
  ch->blocks_recvd = 0;
  ch->filesize = 0;
  ch->transfer_start_time = 0;

  chat_puts("<CHAT> Okay, file transfer canceled");
  sprintf(s, "%c%c", CHAT_FILE_CANCEL, CHAT_END_OF_COMMAND);
  strcat(ch->writeq, s);
  process_output(ch);
}

/*
------------------------------------------------------------------------------
  filestat() -- Get statistics on the file transfer in progress.
------------------------------------------------------------------------------
*/
void filestat(const char *name)
{
  char s[MAX_STRING_LENGTH];
  char left[MAX_STRING_LENGTH];
  struct chat_data *ch;
  struct timeval now;
  struct timezone z = {0, 0};
  int bps = 0;

  name = get_arg_in_braces(name, left, 1);
  if (!*left)
    return;
  if ((ch = get_ch_by_name(left)) == NULL)
    return;
  if (!*(ch->filename))
    chat_puts("# No file transfer in progress with that person.");
  else {
    gettimeofday(&now, &z);
    bps = (ch->blocks_recvd * 500) / (now.tv_sec - ch->transfer_start_time);
    sprintf(s, "FILE TRANSFER STATUS: [%s]", ch->chatname);
    chat_puts(s);
    sprintf(s, "Filename: [%12s] -- Filesize: [%5d]", ch->filename, ch->filesize); 
    chat_puts(s);
    sprintf(s, "Bytes received: [%6d] -- Bytes/sec: [%4d]", ch->blocks_recvd * 500, bps);
    chat_puts(s);
  }
}
/*************************************************************************
 * INTERNAL UTILITY ROUTINES                                             *
 ************************************************************************/


/*
------------------------------------------------------------------------------
  read_chat_defaults() -- Reads from a text file the default information
                          for the chat connections.  (chat.conf)
------------------------------------------------------------------------------
*/
void read_chat_defaults()
{
  FILE *f;
  char s[MAX_STRING_LENGTH];
  char tag[MAX_STRING_LENGTH];
  char val[MAX_STRING_LENGTH];
  char filestring[MAX_STRING_LENGTH];
  
  if (getenv("TINTIN_CONF") == NULL)
    sprintf(filestring, "%s/%s", getenv("HOME"), CONFIG_FILE);
  else
    sprintf(filestring, "%s/%s", getenv("TINTIN_CONF"), CONFIG_FILE);

  if ((f = fopen(filestring, "r")) == NULL) {
    printf("\n#ERROR: tt.conf not found!\n");
    printf("        This file must be in your home directory.\n");
    printf("        Using defaults...\n");
    fflush(stdout);
    return;
  }
  
  while(fgets(s, MAX_STRING_LENGTH - 1, f) != NULL) {
    if (!*s)
      continue;
    if (*s == '#') /* comment marker */
      continue;
    sscanf(s, "%s : %[^~]~", tag, val);
    if (!strcmp("NAME", tag))
      strcpy(my_name, val);
    else if (!strcmp("IPAD", tag))
      strcpy(my_ip, val);
    else if (!strcmp("PORT", tag))
      my_port = atoi(val);
    else if (!strcmp("OGTO", tag))
      outgoing_call_timeout = atoi(val);
    else if (!strcmp("ICTO", tag))
      incoming_call_timeout = atoi(val);
    else if (!strcmp("AUTO", tag))
      auto_accept = atoi(val);
    else if (!strcmp("DLOD", tag))
      strcpy(dl_path, val);
    else if (!strcmp("BIND", tag))
      rebind = atoi(val);
    else if (!strcmp("ENABLE", tag))
      enable_chat = atoi(val);
    else
      printf("#ERROR: Invalid tag %s in tt.conf", tag);
  }
  fflush(stdout);
  fclose(f);
}

/*
------------------------------------------------------------------------------
  get_file_size() -- Returns the size in bytes of the file specified by
                     fpath.
------------------------------------------------------------------------------
*/
/* returns the size of a file in bytes (0 on error) */
int get_file_size(char *fpath)
{
  struct stat statbuf;

  if (stat(fpath, &statbuf) == -1)
    return 0;
  return statbuf.st_size;
}

/*
------------------------------------------------------------------------------
  skip_spaces() -- Move pointer s to the first non-space character.
------------------------------------------------------------------------------
*/
void skip_spaces(char **s)
{
  while (**s == ' ')
    (*s)++;
}

/*
------------------------------------------------------------------------------
  cpylower() -- Copy the string "from" to the string "to" and replace all
                upercase characters with their lowercase counterparts.
------------------------------------------------------------------------------
*/
void cpylower(char *to, char *from)
{
  while (*from) {
    *to = tolower(*from);
    to++;
    from++;
  }
  *to = '\0';
}

/*
------------------------------------------------------------------------------
  chat_puts() -- Writes text to user's screen. 
                 (All chat functions that print output call this.)
------------------------------------------------------------------------------
*/
void chat_puts(char *arg)
{
  char s[MAX_STRING_LENGTH];

  sprintf(s, "%s%s%s", chat_color, arg, COLOR_NORMAL);
  tintin_puts2(s, NULL);
}

/*
------------------------------------------------------------------------------
  get_dynamic_ip() -- For those using pppd and have a DHCP server, this
                      should retrieve your IP address.  This is a hack!
                      I do not claim that it will work in every instance.
                      If you know a better way to do this, please send me
                      email:  sbutler@deveast.com
------------------------------------------------------------------------------
*/
int get_dynamic_ip(char *ip)
{
  FILE *pidfile;
  FILE *msgfile;
  char s[MAX_STRING_LENGTH];
  int pppd_pid = 0;
  char tmp1[MAX_STRING_LENGTH];
  char tmp2[MAX_STRING_LENGTH];
  char tmp3[MAX_STRING_LENGTH];
  char tmp4[MAX_STRING_LENGTH];

  int pid;

  if ((pidfile = fopen("/var/run/ppp0.pid", "r")) == NULL) {
    chat_puts("Failed to lookup dynamic IP address");
    return 0;
  }
  if (fgets(s, MAX_STRING_LENGTH -1, pidfile) == NULL) {
    chat_puts("Failed to lookup dynamic IP address");
    fclose(pidfile);
    return 0;
  }
  sscanf(s, "%d", &pppd_pid);
  fclose(pidfile);
  if (!pppd_pid) {
    chat_puts("Failed to lookup dynamic IP address");
    return 0;
  } 
  /* now we have the pppd pid */
  if ((msgfile = fopen("/var/adm/messages", "r")) == NULL) {
    chat_puts("Failed to lookup dynamic IP address");
    return 0;
  }
  
  while(fgets(s, MAX_STRING_LENGTH -1, msgfile) != NULL) {
    if(sscanf(s, "%s %s %s %s pppd[%d]: local  IP address %s", tmp1, tmp2, tmp3, tmp4, &pid, ip) < 6)
      continue;
    if (pid == pppd_pid) {
      fclose(msgfile);
      return 1;
    }
  }
  fclose(msgfile);
  *ip = '\0';
  return 0;
}
/*
------------------------------------------------------------------------------
  get_ch_by_name() -- Given a possibly abbreviated name, return the first
                      chatname match in the list of connections.
------------------------------------------------------------------------------
*/
struct chat_data *get_ch_by_name(char *arg)
{
  char s[SHORT_STRING];
  char t[SHORT_STRING];
  struct chat_data *ch;
  
  cpylower(s, arg);
  for (ch = chat_list; ch; ch = ch->next) {
    cpylower(t, ch->chatname);
    if (!strncmp(t, s, strlen(s)))
      return ch;
  }
  return NULL;
}

/*
------------------------------------------------------------------------------
strip_path_info() -- For security reasons we don't want people sending files
                     that contain a path, all files will be stored in the
                     receiver's download directory.
                     Since this will be compatible with mm we must be sure to
                     deal with DOS file name characters that could cause the
                     receiver trouble. i.e. #sendfile Dragos c:command.com
------------------------------------------------------------------------------
*/
int strip_path_info(char *name)
{
  int len;
  char *s;
  
  for(len = strlen(name); len > 0; len--) {
    s = name + len - 1;
    if ((*s == '/') || (*s == '\\') || (*s == ':')) {
      strcpy(name, s + 1);
      break;
    }
  }
  return strlen(name);
}








