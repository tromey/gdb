#include "defs.h"

#undef PACKAGE_NAME
#undef PACKAGE
#undef PACKAGE_VERSION
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_BUGREPORT
#undef PACKAGE_URL
#undef VERSION

#include "gdbserver/server.h"
#include "gdbserver/target.h"
#include "sim/callback.h"
#include "sim/sim.h"

extern host_callback default_callback;

struct sim_target : public process_stratum_target
{
  sim_target ()
  {
    default_callback.init (&default_callback);
    m_sim = sim_open (SIM_OPEN_STANDALONE, &default_callback,
		      nullptr, nullptr);
  }

  ~sim_target ()
  {
    if (m_sim != nullptr)
      sim_close (m_sim, 1);
  }

  int create_inferior (const char *program,
		       const std::vector<char *> &program_args) override;

  int attach (unsigned long pid) override
  {
    return -1;
  }

  int kill (process_info *proc) override
  {
    sim_close (m_sim, 1);
    m_sim = nullptr;
    return 0;
  }

  int detach (process_info *proc) override
  {
    return -1;
  }

  void mourn (process_info *proc) override
  {
    sim_close (m_sim, 0);
    m_sim = nullptr;

    remove_process (proc);
  }

  void join (int pid) override
  {
    // FIXME?
  }

  bool thread_alive (ptid_t pid) override
  {
    return m_sim != nullptr;
  }

  void resume (thread_resume *resume_info, size_t n) override
  {
    gdb_assert (n == 1);
    m_resume = resume_info[0];
  }

  ptid_t wait (ptid_t ptid, target_waitstatus *status,
	       target_wait_flags options) override
  {
    // FIXME do we really need to handle resume_stop here?
    if (m_resume.kind == resume_stop)
      sim_stop (m_sim);
    else
      sim_resume (m_sim, m_resume.kind == resume_step, m_resume.sig);

    // FIXME handle options?
    enum sim_stop reason = sim_running;
    int sigrc;
    sim_stop_reason (m_sim, &reason, &sigrc);

    switch (reason)
      {
      case sim_exited:
	status->set_exited (sigrc);
	break;
      case sim_stopped:
	switch (sigrc)
	  {
	  case GDB_SIGNAL_ABRT:
	    quit ();
	    break;
	  case GDB_SIGNAL_INT:
	  case GDB_SIGNAL_TRAP:
	  default:
	    status->set_stopped ((gdb_signal) sigrc);
	    break;
	  }
	break;
      case sim_signalled:
	status->set_signalled ((gdb_signal) sigrc);
	break;
      case sim_running:
      case sim_polling:
	/* FIXME: Is this correct?  */
	break;
      }

    return ptid;
  }

  void fetch_registers (regcache *regcache, int regno) override
  {
    if (regno == -1)
      {
	for (regno = 0; regno < gdbarch_num_regs (gdbarch); regno++)
	  fetch_registers (regcache, regno);
	return;
      }

    // FIXME: if register does not exist or is
    // "LEGACY_SIM_REGNO_IGNORE", just return here.

    int regsize = register_size (regcache->tdesc, regno);
    gdb::byte_vector buf (regsize, 0);

    sim_fetch_register (m_sim, regno, buf.data (), regsize);
    regcache->raw_supply (regno, buf.data ());
  }

  void store_registers (regcache *regcache, int regno) override
  {
    if (regno == -1)
      {
	for (regno = 0; regno < gdbarch_num_regs (gdbarch); regno++)
	  store_registers (regcache, regno);
	return;
      }

    // FIXME: if register does not exist or is
    // "LEGACY_SIM_REGNO_IGNORE", just return here.

    int regsize = register_size (regcache->tdesc, regno);
    gdb::byte_vector buf (regsize, 0);

    regcache->raw_collect (regno, buf);
    sim_store_register (m_sim, regno, buf.data (), regsize);
  }

  int read_memory (CORE_ADDR memaddr, unsigned char *myaddr, int len) override
  {
    if (sim_read (m_sim, memaddr, myaddr, len) == len)
      return 0;
    return 1;
  }

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr, int len)
    override
  {
    if (sim_write (m_sim, memaddr, myaddr, len) == len)
      return 0;
    return 1;
  }

  void request_interrupt ()
  {
    sim_stop (m_sim);
  }

  int handle_monitor_command (char *mon) override
  {
    sim_do_command (m_sim, mon);
    return 1;			// FIXME
  }

  const gdb_byte *sw_breakpoint_from_kind (int kind, int *size)
  {
    /* We use the default supports_z_point_type, so currently this
       should never be called.  */
    return nullptr;
  }

private:

  /* Store the most recent resume request until the wait method is
     called.  */
  thread_resume m_resume;

  SIM_DESC m_sim;
};

static sim_target *the_sim_target;

void
initialize_low ()
{
  the_sim_target = new sim_target ();
  set_target_ops (the_sim_target);
}
