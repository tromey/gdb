#include "gdbserver/target.h"

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

  int detach (process_info *proc) override
  {
    return -1;
  }

  void mourn (process_info *proc) override
  {
    sim_close (m_sim, fixme);
    m_sim = nullptr;
  }

  void request_interrupt () const
  {
    sim_stop (m_sim);
  }

  void resume (thread_resume *resume_info, size_t n) override
  {
    sim_resume (m_sim, fixme);
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
