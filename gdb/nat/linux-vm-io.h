/* Wrappers for Linux process_vm_readv and process_vm_writev
   Copyright (C) 2014 Free Software Foundation, Inc.

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

#ifndef NAT_LINUX_VM_IO_H
#define NAT_LINUX_VM_IO_H

extern int linux_process_vm_read (pid_t pid,
				  CORE_ADDR memaddr, gdb_byte *buf,
				  ULONGEST len,
				  ULONGEST *xfered_len);

extern int linux_process_vm_write (pid_t pid,
				   CORE_ADDR memaddr, const gdb_byte *buf,
				   ULONGEST len,
				   ULONGEST *xfered_len);

#endif /* NAT_LINUX_VM_IO_H */
