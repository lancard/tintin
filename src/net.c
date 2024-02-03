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
/* Autoconf patching by David Hedbor, neotron@lysator.liu.se */
/*********************************************************************/
/* file: net.c - do all the net stuff                                */
/*                             TINTIN III                            */
/*          (T)he K(I)cki(N) (T)ickin D(I)kumud Clie(N)t             */
/*                     coded by peter unold 1992                     */
/*********************************************************************/

#include "config.h"
#include "tintin.h"

#if defined(HAVE_STRING_H)
#include <string.h>
#elif defined(HAVE_STRINGS_H)
#include <strings.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <arpa/inet.h>

#ifdef HAVE_NET_ERRNO_H
#include <net/errno.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef SOCKS
#include <socks.h>
#endif

#include "include/main.h"
#include "include/net.h"
#include "include/parse.h"
#include "include/rl.h"
#include "include/utils.h"
#include "include/log.h"

#ifndef BADSIG
#define BADSIG (RETSIGTYPE (*)(int))-1
#endif

static RETSIGTYPE alarm_handler(int no_care)
{
  /* do nothing; the connect will fail, returning -1, with errno = EINTR */
}

/**************************************************/
/* try connect to the mud specified by the args   */
/* return fd on success / 0 on failure            */
/**************************************************/

int connect_mud(const char *host, const char *port, struct session *ses)
{
  int sock, connectresult;
  struct sockaddr_in sockaddr;
  char buf[1024];

  if(isdigit(*host))                            /* interprete host part */
    sockaddr.sin_addr.s_addr = inet_addr(host);
  else {
    struct hostent *hp;

    if(!(hp = gethostbyname(host))) {
      tintin_puts("#ERROR - UNKNOWN HOST.", ses);
      prompt(NULL); 
      return(0);
    }
    memcpy((char *)&sockaddr.sin_addr, hp->h_addr, sizeof(sockaddr.sin_addr));
  }

  if(isdigit(*port))  
    sockaddr.sin_port = htons(atoi(port));      /* inteprete port part */
  else {
    tintin_puts("#THE PORT SHOULD BE A NUMBER.", ses);
    prompt(NULL);
    return(0);
  }

  if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    syserr("socket");

  sockaddr.sin_family = AF_INET;


  tintin_puts("#Trying to connect..", ses);

  /* ignore the alarm: it'll cause the connect to return -1 with errno=EINTR */
  if(signal(SIGALRM, alarm_handler) == BADSIG)
    syserr("signal SIGALRM");
  alarm(30);         /* We'll allow connect to hang in 15seconds! NO MORE! */

  connectresult = connect(sock, (struct sockaddr *)&sockaddr, sizeof(sockaddr));

  alarm(0);

  if(connectresult) {
    close(sock);
    switch(errno) {
    case EINTR:
      tintin_puts("#CONNECTION TIMED OUT.", ses);
      break;
    case ECONNREFUSED:
      tintin_puts("#ERROR - CONNECTION REFUSED.", ses);
      break;
    case ENETUNREACH:
      tintin_puts("#ERROR - THE NETWORK IS NOT REACHABLE FROM THIS HOST.", ses);
      break;
    default:
      if (strerror(errno)) {
        sprintf(buf, "#ERROR - %s.", strerror(errno));
        tintin_puts(buf, ses);
      }

    } 
    prompt(NULL);
    return(0);
  }
  return(sock);
}

/************************************************************/
/* write line to the mud ses is connected to - add \n first */
/************************************************************/

void write_line_mud(const char *line, struct session *ses)
{
  char outtext[BUFFER_SIZE+2];

  if (!ZOMBI_IS_ALIVE(ses))  /* Fixed. Connection test -- ycjhi */
    return ;

  sprintf(outtext, "%s", line);
  /* lost this fix somehow.  It appears that I lost it during
     1.81 Might have lost it when I intergrated the zombie
     code -- DSC */
  if (broken_telnet)
    strcat(outtext, "\n");
  else
    strcat(outtext, "\r\n");
    
  if(write(ses->socket, outtext, strlen(outtext)) == -1)
    syserr("write in write_to_mud");
}


/*******************************************************************/
/* read at most BUFFER_SIZE chars from mud - parse protocol stuff  */
/*******************************************************************/

int read_buffer_mud(char *buffer, struct session *ses)
{
  int i, didget;
  char tmpbuf[BUFFER_SIZE], *cpsource, *cpdest;
  tmpbuf[0] = '\0';
  didget = read(ses->socket, tmpbuf, 512);
  ses->old_more_coming = ses->more_coming;
  ses->more_coming = (didget == 512 ? 1 : 0);

  if(didget < 0) 
    return(0); /*syserr("read from socket");  we do this here instead - dunno quite
                why, but i got some mysterious connection read by peer on some hps */
  
  else if(!didget)
    return(0);
  else {
    cpsource = tmpbuf;
    cpdest = buffer;
    i = didget;

    translate_telnet_protocol(cpdest, cpsource, i);

  }
  return(didget);
}

void translate_telnet_protocol(unsigned char *dst, unsigned char *src, 
				 size_t srclen)
{
    char buf[100];
    int skip_telnet = 0;
    unsigned char *cpdst;
    unsigned char *cpsrc;
    
    cpdst = dst;
    cpsrc = src ;

    while(srclen) {
      if(*cpsrc == TELNET_IAC) {  /* if telnet command */
	    sprintf(buf, "%X, %X, %X", cpsrc[1], cpsrc[2], cpsrc[3]);
	      tintin_puts2(buf, NULL); 
	switch(cpsrc[1]) {
	  /* 2-byte commands ---- begin */
	case TELNET_SE:    skip_telnet = 2 ; break ;
	case TELNET_NOP:   skip_telnet = 2 ; break ;
	case TELNET_DM:    skip_telnet = 2 ; break ;
	case TELNET_BREAK: skip_telnet = 2 ; break ;
	case TELNET_IP:    skip_telnet = 2 ; break ;
	case TELNET_AO:    skip_telnet = 2 ; break ;
	case TELNET_AYT:   skip_telnet = 2 ; break ;
	case TELNET_EC:    skip_telnet = 2 ; break ;
	case TELNET_EL:    skip_telnet = 2 ; break ;
	case TELNET_GA:    *cpdst++ = '\n' ;
                                   skip_telnet = 2 ;
                                   break ;
	case TELNET_SB:    skip_telnet = 2 ; break ;
	  /* 2-byte commands ---- end */
	  /* 3-byte commands ---- begin */
	case TELNET_WILL:  skip_telnet = 3 ; 
	  break ;
	case TELNET_WONT:  skip_telnet = 3 ; 
	  break ;
	case TELNET_DO:    skip_telnet = 3 ; break ;
	case TELNET_DONT:  skip_telnet = 3 ; break ;
	  /* 3-byte commands ---- end */

	  /* data commands   ---- begin */
	case TELNET_IAC:   *cpdst++ = 0xFF ;
                                   skip_telnet = 2 ;
                                   break ;
				   /* data commands   ---- end */
				   /* unknown telnet commands gonna be 
				      just skipped. */

	default:           skip_telnet = 1 ; break ;
	} /* switch */
	srclen -= skip_telnet ;
	cpsrc  += skip_telnet ;
      } else {                   /* if not telnet command */
	/* skip (char)0 in socket input */
	if(*cpsrc==0)
	  cpsrc++;
	else
	*cpdst++ = *cpsrc++ ;
	srclen -- ;
      } /* if */
    } /* while */
    *cpdst = '\0' ;

}
