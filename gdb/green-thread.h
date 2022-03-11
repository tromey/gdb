/* Green threads

   Copyright (C) 2022 Free Software Foundation, Inc.

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

#ifndef GREEN_THREAD_H
#define GREEN_THREAD_H

#include "inferior.h"

class green_thread : public private_thread_info
{
public:

  ptid_t get_ptid () const
  {
    return m_ptid;
  }

  virtual std::string get_pid_name () const = 0;

  // Return null ptid if this thread is currently not running on a
  // real thread.
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

extern struct thread_info *add_green_thread
     (std::unique_ptr<green_thread> &&gth);

#endif /* GREEN_THREAD_H */
