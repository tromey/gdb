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

#ifdef GDBSERVER
#include "server.h"
#else
#include "defs.h"
#include <string.h>
#endif

#include "linux-vm-io.h"

#include <unistd.h>
#include <errno.h>
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#if defined (HAVE_PROCESS_VM_READV) || defined (HAVE_PROCESS_VM_WRITEV)

#include "vec.h"

typedef struct iovec iovec_struct;
DEF_VEC_O (iovec_struct);

static VEC (iovec_struct) *
iovec_split (CORE_ADDR memaddr, ULONGEST len)
{
  unsigned long pagesize = getpagesize ();
  VEC (iovec_struct) *result = NULL;
  struct iovec temp;

  /* The first entry aligns up to the next page.  */
  if ((memaddr % pagesize) != 0)
    {
      ULONGEST nbytes;

      /* Number of bytes left on page.  */
      nbytes = memaddr - (memaddr % pagesize);
      if (nbytes > len)
	nbytes = len;

      temp.iov_base = (void *) memaddr;
      temp.iov_len = nbytes;
      VEC_safe_push (iovec_struct, result, &temp);

      len -= nbytes;
      memaddr += nbytes;
    }

  /* Each remaining entry is a single page.  */
  while (len > 0)
    {
      ULONGEST nbytes = len;

      if (nbytes > pagesize)
	nbytes = pagesize;

      temp.iov_base = (void *) memaddr;
      temp.iov_len = nbytes;
      VEC_safe_push (iovec_struct, result, &temp);

      len -= nbytes;
      memaddr += nbytes;
    }

  return result;
}

#endif

int
linux_process_vm_read (pid_t pid,
		       CORE_ADDR memaddr, gdb_byte *buf, ULONGEST len,
		       ULONGEST *xfered_len)
{
#ifdef HAVE_PROCESS_VM_READV
  ssize_t result;
  struct iovec local;
  VEC (iovec_struct) *remotes;

  local.iov_base = buf;
  local.iov_len = len;

  remotes = iovec_split (memaddr, len);
  result = process_vm_readv (pid, &local, 1,
			     VEC_address (iovec_struct, remotes),
			     VEC_length (iovec_struct, remotes), 0);
  VEC_free (iovec_struct, remotes);

  if (result == -1)
    {
      if (errno == EFAULT)
	errno = ENOTSUP;
      return -1;
    }

  *xfered_len = result;
  return 0;

#else /* HAVE_PROCESS_VM_READV */
  errno = ENOTSUP;
  return -1;
#endif
}

int
linux_process_vm_write (pid_t pid,
			CORE_ADDR memaddr, const gdb_byte *buf, ULONGEST len,
			ULONGEST *xfered_len)
{
#ifdef HAVE_PROCESS_VM_WRITEV
  ssize_t result;
  struct iovec local;
  VEC (iovec_struct) *remotes;

  /* We have to cast away const to match iovec, but it will never
     actually be written to.  */
  local.iov_base = (void *) buf;
  local.iov_len = len;

  remotes = iovec_split (memaddr, len);
  result = process_vm_writev (pid, &local, 1,
			      VEC_address (iovec_struct, remotes),
			      VEC_length (iovec_struct, remotes), 0);
  VEC_free (iovec_struct, remotes);

  if (result == -1)
    {
      /* blah?? !?! */
      if (errno == EFAULT)
	errno = ENOTSUP;
      return -1;
    }

  *xfered_len = result;
  return 0;

#else /* HAVE_PROCESS_VM_WRITEV */
  errno = ENOTSUP;
  return -1;
#endif
}
