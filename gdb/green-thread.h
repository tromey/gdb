/* Green threads

   Copyright (C) 2022, 2025, 2026 Free Software Foundation, Inc.

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

#ifndef GDB_GREEN_THREAD_H
#define GDB_GREEN_THREAD_H

#include "inferior.h"

/* A "green" thread is some kind of user-space thread or task.  For
   example, a user-space threading system implemented using
   'makecontext' might expose the different contexts as green threads
   in gdb.

   A green thread has two modes.

   One mode is "active".  In this case the green thread is currently
   running some real underlying thread.  The green thread can be
   stepped, continued, etc.  In this situation, the registers are
   found by querying the underlying system thread.

   A green thread can also be "suspended".  In this case it isn't
   currently running.  It can be selected and the registers examined
   (and the stack viewed, etc) -- but stepping and other inferior
   control operations aren't available.  In this situation, the
   registers are found via the green thread; normally they would be
   stored in memory somewhere.  */
class green_thread : public private_thread_info
{
public:

  /* Return the ptid of this thread.  */
  ptid_t get_ptid () const
  {
    return m_ptid;
  }

  /* Return a string representation of this thread's ptid.  Note that
     this isn't a user-supplied thread name, but instead similar to
     the target method of the same name.  */
  virtual std::string pid_to_str () const = 0;

  /* Return the ptid of the underlying thread, or null ptid if this
     thread is suspended.  */
  virtual ptid_t underlying_thread () const = 0;

  virtual void fetch_registers (struct regcache *regcache, int regnum)
    const = 0;
  virtual void store_registers (struct regcache *regcache, int regnum)
    const = 0;

protected:

  explicit green_thread (ULONGEST tid)
    : m_ptid (inferior_ptid.pid (), 0, tid)
  {
  }

private:

  /* The thread ID.  */
  ptid_t m_ptid;
};

using green_thread_up = std::unique_ptr<green_thread>;

/* While a green thread can be queried directly to see the underlying
   thread, we also need a way to see if a given "native" thread is
   currently hosting a green thread.  A provider is used to do this.
   Providers are passed in when a green thread is created, but not
   that their lifetime is not tied to the thread at all -- once
   created they should live as long as gdb.  */

struct green_provider
{
  /* Return the ptid of the green thread that is currently active on
     the current selected thread, which will be a non-green (native)
     thread.  Returns null ptid if no such mapping exists.  There's an
     important difference between this method and iterating over all
     green threads, calling underlying_thread -- this method should
     avoid races by consulting data structures local to the selected
     thread when finding the result.  */
  virtual ptid_t current_green_thread () const = 0;
};

/* Add a new green thread.  GTH is the thread to add.  PROVIDER is the
   provider.  Note that it is normal for a given provider to be reused
   -- that is, supplied for many green threads.  */
extern struct thread_info *add_green_thread (green_thread_up gth,
					     green_provider *provider);

#endif /* GDB_GREEN_THREAD_H */
