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
  }

  void mourn (int pid) override
  {
    sim_close (m_sim, 0);
    m_sim = nullptr;
  }

  void request_interrupt () const
  {
    sim_stop (m_sim);
  }

  void resume (thread_resume *resume_info, size_t n) override
  {
    // fixme sim_resume (m_sim, fixme);
  }

  int handle_monitor_command (char *mon) override
  {
    sim_do_command (m_sim, mon);
    return 1;			// FIXME
  }

  int read_memory (CORE_ADDR memaddr, unsigned char *myaddr, int len) override
  {
    // fixme return value
    return sim_read (m_sim, memaddr, myaddr, len);
  }

  int write_memory (CORE_ADDR memaddr, const unsigned char *myaddr, int len)
    override
  {
    // fixme return value
    return sim_write (m_sim, mem, myaddr, len);
  }

private:
  SIM_DESC m_sim;		// FIXME??
};

void
blah_blah ()
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
