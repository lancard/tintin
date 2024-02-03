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
#ifndef __CHAT_H__
#define __CHAT_H__
extern int chat_init();
extern void chat_process_connections(fd_set *input_set, fd_set *exc_set);
extern int chat_new(int s);
extern int chat_ping(const char *arg);
extern int ping_response(struct chat_data *ch, char *time);
extern int process_input(struct chat_data *ch);
extern void process_output(struct chat_data *ch);
extern int get_chat_commands(struct chat_data *ch);
extern int parse_connections(char *txt);
extern int get_dynamic_ip(char *ip);
extern int request_response(struct chat_data *req);
extern int request_conns(const char *arg);
extern void close_chat(struct chat_data *ch);
extern void nonblock(int s);
extern void block(int s);
extern void read_typing(void);
extern void chat_message(char *msg, struct chat_data *ch);
extern void skip_spaces(char **s);
extern void cpylower(char *to, char *from);
extern void chat_puts(char *arg);
extern void chat_call(const char *ip);
extern void read_chat_defaults();
extern void file_denied(struct chat_data *ch, char *txt);
extern int strip_path_info(char *name);
extern struct chat_data *get_ch_by_name(char *arg);
extern int recv_file(char *arg, struct chat_data *ch);
extern void deny_file(struct chat_data *ch, char *arg);
extern int chat_xfer_toggle(const char *arg);
extern int tt_send_file(const char *arg);
extern int recv_block(unsigned char *s, struct chat_data *ch);
extern int send_block(struct chat_data *ch);
extern void req_next_block(struct chat_data *ch);
extern void read_chat_defaults();
extern void chat_all(const char *txt);
extern void emote_all(const char *txt);
extern void emote(const char *txt);
extern void chat(const char *txt);
extern void unchat(const char *name);
extern void chatname(const char *name);
extern void display_cinfo();
extern void chatpriv(const char *name);
extern int file_block_send(struct chat_data *ch);
extern void file_cancel(const char *name);
extern void filestat(const char *name);
extern void read_chat_defaults();
extern int get_file_size(char *fpath);
#endif

