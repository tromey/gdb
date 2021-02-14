#include "gdbserver/target.h"
#include "gdb/callback.h"
#include "gdb/remote-sim.h"

class sim_target : public process_stratum_target
{
  ~sim_target ()
  {
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
  }

  bool thread_alive (ptid_t pid) override;

  void resume (thread_resume *resume_info, size_t n) override
  {
    gdb_assert (n == 1);
    m_resume = resume_info[0];
  }

  ptid_t wait (ptid_t ptid, target_waitstatus *status,
	       target_wait_flags options) override;
  {
    if (m_resume.kind == resume_stop)
      sim_stop (m_sim);
    else
      sim_resume (m_sim, m_resume.kind == resume_step, m_resume.sig);
    // FIXME options?
    // FIXME *status = ;
    return ptid;
  }

  void fetch_registers (regcache *regcache, int regno) override;

  void store_registers (regcache *regcache, int regno) override;

  int handle_monitor_command (char *mon) override
  {
    sim_do_command (m_sim, mon);
    return 1;			// FIXME
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
    if (sim_write (m_sim, mem, myaddr, len) == len)
      return 0;
    return 1;
  }

  void request_interrupt () const
  {
    sim_stop (m_sim);
  }

private:

  /* Store the most recent resume request until the wait method is
     called.  */
  thread_resume m_resume;

  SIM_DESC m_sim = nullptr;
};

void
initialize_sim ()
{
  gdb_callback = default_callback;
  gdb_callback.init (&gdb_callback);
  gdb_callback.write_stdout = gdb_os_write_stdout;
  gdb_callback.flush_stdout = gdb_os_flush_stdout;
  gdb_callback.write_stderr = gdb_os_write_stderr;
  gdb_callback.flush_stderr = gdb_os_flush_stderr;
  gdb_callback.printf_filtered = gdb_os_printf_filtered;
  gdb_callback.vprintf_filtered = gdb_os_vprintf_filtered;
  gdb_callback.evprintf_filtered = gdb_os_evprintf_filtered;
  gdb_callback.error = gdb_os_error;
  gdb_callback.poll_quit = gdb_os_poll_quit;
  gdb_callback.magic = HOST_CALLBACK_MAGIC;
  callbacks_initialized = 1;
}
