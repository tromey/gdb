/* Tasks and task pool for GDB.

   Copyright (C) 2018 Free Software Foundation, Inc.

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

#include "common-defs.h"
#include "common/task-pool.h"

class task_pool::task
{
  explicit task (std::string &&name, std::function<void ()> &&func,
		 size_t priority)
    : m_name (std::move (name)),
      m_func (std::move (func)),
      m_priority (priority)
  {
  }

  task (task &&other) = default;

  void run ()
  {
    TRY
      {
	m_func ();
      }
    CATCH (ex, RETURN_MASK_ALL)
      {
	/* Ignore.  */
      }
    END_CATCH
  }

  void notify ()
  {
    m_cond.notify_all ();
  }

  void wait (std::unique_lock<std::mutex> &mutex)
  {
    m_cond.wait (mutex);
  }

  bool operator< (const task &other) const
  {
    return m_priority < other.m_priority;
  }

private:

  friend class task_pool;

  DISABLE_COPY_AND_ASSIGN (task);

  bool m_started = false;
  bool m_finished = false;
  std::string m_name;
  std::function<void ()> m_func;  
  std::condition_variable m_cond;
  size_t m_priority;
};

bool
task_pool::compare_tasks::operator() (const std::shared_ptr<task> &a,
				      const std::shared_ptr<task> &b)
{
  return *a < *b;
}

void
task_pool::worker ()
{
  while (true)
    {
      std::shared_ptr<task> job;

      {
	std::unique_lock<std::mutex> lock (m_lock);

	while (m_queue.empty ())
	  m_var.wait (lock);

	job = m_queue.top ();
	m_queue.pop ();
	if (job->m_started)
	  continue;
	job->m_started = true;
      }

      job->run ();

      {
	std::unique_lock<std::mutex> lock (m_lock);

	job->m_finished = true;
	job->notify ();
      }
    }
}

void
task_pool::run (std::shared_ptr<task> &job)
{
  {
    std::unique_lock<std::mutex> lock (m_lock);
    if (job->m_started)
      {
	while (!job->m_finished)
	  job->wait (lock);
	return;
      }

    job->m_started = true;
  }

  job->run ();

  {
    std::unique_lock<std::mutex> lock (m_lock);

    job->m_finished = true;
    job->notify ();
  }
}

task_pool::task_pool (unsigned int n_threads)
{
  if (n_threads == 0)
    n_threads = std::thread::hardware_concurrency ();
  if (n_threads == 0)
    n_threads = 2;

  for (unsigned int i = 0; i < n_threads; ++i)
    {
      std::thread t (&task_pool::worker, this);
      t.detach ();
    }
}

std::shared_ptr<task_pool::task>
task_pool::add_task (std::string &&name, std::function<void ()> &&func,
		     size_t priority)
{
  std::shared_ptr<task> job (new task (std::move (name), std::move (func),
				       priority));
  std::unique_lock<std::mutex> lock (m_lock);
  m_queue.push (job);
  m_var.notify_one ();
  return job;
}
