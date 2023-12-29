/* Remote File-I/O communications

   Copyright (C) 2003-2023 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* See the GDB User Guide for details of the GDB remote protocol.  */

#ifndef REMOTE_FILEIO_H
#define REMOTE_FILEIO_H

#include "gdbsupport/fileio.h"

struct cmd_list_element;
struct remote_target;

/* This holds the state needed by the remote fileio code.  */

struct remote_fileio_data
{
public:

  void request (remote_target *remote,
		char *buf, int ctrlc_pending_p);

  void reset ();

private:

  int fd_to_targetfd (int fd);
  int map_fd (int target_fd);
  void close_target_fd (int target_fd);

  void func_open (remote_target *remote, char *buf);
  void func_close (remote_target *remote, char *buf);
  void func_read (remote_target *remote, char *buf);
  void func_write (remote_target *remote, char *buf);
  void func_lseek (remote_target *remote, char *buf);
  void func_rename (remote_target *remote, char *buf);
  void func_unlink (remote_target *remote, char *buf);
  void func_stat (remote_target *remote, char *buf);
  void func_fstat (remote_target *remote, char *buf);
  void func_gettimeofday (remote_target *remote, char *buf);
  void func_isatty (remote_target *remote, char *buf);
  void func_system (remote_target *remote, char *buf);
  void do_request (remote_target *remote, char *buf);

  int init_fd_map ();
  int resize_fd_map ();
  int next_free_fd ();

  std::vector<int> m_fd_map;
};

/* Unified interface to remote fileio, called in remote.c from
   remote_wait () and remote_async_wait ().  */
extern void remote_fileio_request (remote_target *remote,
				   char *buf, int ctrlc_pending_p);

/* Cleanup any remote fileio state.  */
extern void remote_fileio_reset (void);

/* Called from _initialize_remote ().  */
extern void initialize_remote_fileio (
  struct cmd_list_element **remote_set_cmdlist,
  struct cmd_list_element **remote_show_cmdlist);

/* Unpack a struct fio_stat.  */
extern void remote_fileio_to_host_stat (struct fio_stat *fst,
					struct stat *st);

#endif
