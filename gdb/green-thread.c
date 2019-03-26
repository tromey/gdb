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

#include "defs.h"
#include "target.h"
#include "green-thread.h"
#include "regcache.h"

static const target_info green_target_info =
{
  "green threads",
  N_("Green threads"),
  N_("Green thread support.")
};

struct green_thread_target final : public target_ops
{
  green_thread_target () = default;

  const target_info &info () const override
  { return green_target_info; }

  strata stratum () const override { return green_stratum; }

  ptid_t wait (ptid_t, struct target_waitstatus *, target_wait_flags) override;
  void resume (ptid_t, int, enum gdb_signal) override;

  bool can_randomly_thread_switch () override
  {
    return true;
  }

  void fetch_registers (struct regcache *, int) override;
  void store_registers (struct regcache *, int) override;

  void prepare_to_store (struct regcache *) override;

  bool stopped_by_sw_breakpoint () override;

  bool stopped_by_hw_breakpoint () override;

  bool stopped_by_watchpoint () override;

  bool stopped_data_address (CORE_ADDR *) override;

  enum target_xfer_status xfer_partial (enum target_object object,
					const char *annex,
					gdb_byte *readbuf,
					const gdb_byte *writebuf,
					ULONGEST offset, ULONGEST len,
					ULONGEST *xfered_len) override;

  bool thread_alive (ptid_t ptid) override
  {
    /* We assume that the concrete implementation of the green threads
       will track this and mark the thread as exited.  */
    return true;
  }


  int core_of_thread (ptid_t ptid) override;

  // FIXME?
  // void update_thread_list () override;

  std::string pid_to_str (ptid_t) override;

  const char *extra_thread_info (thread_info *) override;

  /* struct btrace_target_info *enable_btrace (thread_info *tp, */
  /* 					    const struct btrace_config *conf) */
  /*   override */

  void mourn_inferior () override;

  ptid_t get_ada_task_ptid (long lwp, ULONGEST thread) override
  {
    return ptid_t (inferior_ptid.pid (), 0, thread);
  }

  void close () override
  {
    delete this;
  }

private:

  bool m_reentrant_call = false;

  const green_thread *find_green_thread (ptid_t ptid)
  {
    thread_info *thr = find_thread_ptid (current_inferior ()->process_target (),
					 ptid);
    if (thr == nullptr)
      return nullptr;
    return dynamic_cast<green_thread *> (thr->priv.get ());
  }

  /* Like switch_to_thread, but uses the underlying ptid for the
     thread.  If PTID is not a green thread, does nothing and returns
     true.  If the green thread is not active, does nothing and
     returns false.  Otherwise, sets the thread and returns true.  */
  bool set_thread_from_green_thread (ptid_t ptid)
  {
    const green_thread *gth = find_green_thread (ptid);
    if (gth == nullptr)
      return true;

    ptid_t underlying = gth->underlying_thread ();
    if (underlying == null_ptid)
      return false;

    process_stratum_target *proc_target
      = current_inferior ()->process_target ();
    switch_to_thread (find_thread_ptid (proc_target, underlying));
    return true;
  }

  /* /\* Return true iff PTID corresponds to a green thread.  *\/ */
  /* bool is_green_thread (ptid_t ptid) const */
  /* { */
  /*   /\* By construction, green threads have their LWP set to zero. */
  /*      Also make sure that the TID is nonzero.  *\/ */
  /*   return ptid.lwp () == 0 && ptid.tid () != 0; */
  /* } */
};

void
green_thread_target::resume (ptid_t ptid, int step, enum gdb_signal siggnal)
{
  if (ptid != minus_one_ptid && !ptid.is_pid ())
    {
      const green_thread *gth = find_green_thread (ptid);
      if (gth != nullptr)
	{
	  ptid = gth->underlying_thread ();
	  if (ptid == null_ptid)
	    error (_("cannot resume thread"));
	}
    }
  beneath ()->resume (ptid, step, siggnal);
}

ptid_t
green_thread_target::wait (ptid_t ptid,
			   struct target_waitstatus *status,
			   target_wait_flags options)
{
  if (ptid != minus_one_ptid)
    {
      const green_thread *gth = find_green_thread (ptid);
      if (gth != nullptr)
	{
	  ptid = gth->underlying_thread ();
	  if (ptid == null_ptid)
	    error (_("can't wait for inactive thread"));
	}
    }

  return beneath ()->wait (ptid, status, options);
}

std::string
green_thread_target::pid_to_str (ptid_t ptid)
{
  const green_thread *gth = find_green_thread (ptid);
  if (gth != nullptr)
    return gth->get_pid_name ();
  return beneath ()->pid_to_str (ptid);
}

const char *
green_thread_target::extra_thread_info (thread_info *thr)
{
  // FIXME let user code change this?
  if (thr->is_green_thread ())
    return nullptr;
  return beneath ()->extra_thread_info (thr);
}

/* Temporarily set the ptid of a regcache to some other value.  When
   this object is destroyed, the regcache's original ptid is
   restored.  */

class temporarily_change_regcache_ptid
{
public:

  temporarily_change_regcache_ptid (struct regcache *regcache, ptid_t new_ptid)
    : m_regcache (regcache),
      m_save_ptid (regcache->ptid ())
  {
    m_regcache->set_ptid (new_ptid);
  }

  ~temporarily_change_regcache_ptid ()
  {
    m_regcache->set_ptid (m_save_ptid);
  }

private:

  /* The regcache.  */
  struct regcache *m_regcache;
  /* The saved ptid.  */
  ptid_t m_save_ptid;
};

void
green_thread_target::fetch_registers (struct regcache *regcache, int regnum)
{
  const green_thread *gth = find_green_thread (regcache->ptid ());
  if (gth != nullptr)
    {
      ptid_t base = gth->get_ptid ();
      if (base != null_ptid)
	{
	  temporarily_change_regcache_ptid changer (regcache, base);
	  beneath ()->fetch_registers (regcache, regnum);
	}
      else
	gth->fetch_registers (regcache, regnum);
    }
  else
    beneath ()->fetch_registers (regcache, regnum);
}

void
green_thread_target::store_registers (struct regcache *regcache, int regnum)
{
  const green_thread *gth = find_green_thread (regcache->ptid ());
  if (gth != nullptr)
    {
      ptid_t base = gth->get_ptid ();
      if (base != null_ptid)
	{
	  temporarily_change_regcache_ptid changer (regcache, base);
	  beneath ()->store_registers (regcache, regnum);
	}
      else
	gth->store_registers (regcache, regnum);
    }
  else
    beneath ()->store_registers (regcache, regnum);
}

void
green_thread_target::prepare_to_store (struct regcache *regcache)
{
  const green_thread *gth = find_green_thread (regcache->ptid ());
  if (gth != nullptr)
    {
      ptid_t base = gth->get_ptid ();
      if (base != null_ptid)
	{
	  temporarily_change_regcache_ptid changer (regcache, base);
	  beneath ()->prepare_to_store (regcache);
	}
      else
	{
	  /* Nothing.  */
	}
    }
  else
    beneath ()->prepare_to_store (regcache);
}

/* Implement the to_stopped_by_sw_breakpoint target_ops "method".  */

bool
green_thread_target::stopped_by_sw_breakpoint ()
{
  scoped_restore_current_thread saver;
  set_thread_from_green_thread (inferior_ptid);
  return beneath ()->stopped_by_sw_breakpoint ();
}

/* Implement the to_stopped_by_hw_breakpoint target_ops "method".  */

bool
green_thread_target::stopped_by_hw_breakpoint ()
{
  scoped_restore_current_thread saver;
  set_thread_from_green_thread (inferior_ptid);
  return beneath ()->stopped_by_hw_breakpoint ();
}

/* Implement the to_stopped_by_watchpoint target_ops "method".  */

bool
green_thread_target::stopped_by_watchpoint ()
{
  scoped_restore_current_thread saver;
  set_thread_from_green_thread (inferior_ptid);
  return beneath ()->stopped_by_watchpoint ();
}

/* Implement the to_stopped_data_address target_ops "method".  */

bool
green_thread_target::stopped_data_address (CORE_ADDR *addr_p)
{
  scoped_restore_current_thread saver;
  set_thread_from_green_thread (inferior_ptid);
  return beneath ()->stopped_data_address (addr_p);
}

void
green_thread_target::mourn_inferior ()
{
  target_ops *beneath = this->beneath ();
  current_inferior ()->unpush_target (this);
  beneath->mourn_inferior ();
}

/* Implement the to_core_of_thread target_ops "method".  */

int
green_thread_target::core_of_thread (ptid_t ptid)
{
  const green_thread *gth = find_green_thread (ptid);
  if (gth != nullptr)
    {
      ptid = gth->underlying_thread ();
      if (ptid == null_ptid)
	return -1;
    }
  return beneath ()->core_of_thread (ptid);
}

/* Implement the target xfer_partial method.  */

enum target_xfer_status
green_thread_target::xfer_partial (enum target_object object,
				   const char *annex,
				   gdb_byte *readbuf,
				   const gdb_byte *writebuf,
				   ULONGEST offset, ULONGEST len,
				   ULONGEST *xfered_len)
{
  bool was_reentrant = m_reentrant_call;
  scoped_restore restore_reentrant = make_scoped_restore (&m_reentrant_call,
							  true);
  ptid_t ptid = inferior_ptid;

  if (!was_reentrant)
    {
      const green_thread *gth = find_green_thread (ptid);
      if (gth != nullptr)
	{
	  ptid = gth->underlying_thread ();
	  if (ptid == null_ptid)
	    ptid = any_live_thread_of_inferior (current_inferior ())->ptid;
	}
    }

  scoped_restore save_ptid = make_scoped_restore (&inferior_ptid, ptid);
  return beneath ()->xfer_partial (object, annex, readbuf, writebuf,
				   offset, len, xfered_len);
}

struct thread_info *
add_green_thread (std::unique_ptr<green_thread> &&gth)
{
  if (current_inferior ()->target_at (green_stratum) == nullptr)
    current_inferior ()->push_target (target_ops_up (new green_thread_target));

  ptid_t ptid = gth->get_ptid ();
  return add_thread_with_info (current_inferior ()->process_target (),
			       ptid, std::move (gth));
}
