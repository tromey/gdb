/* Job queue

   Copyright (C) 2020 Free Software Foundation, Inc.

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

#ifndef GDBSUPPORT_JOB_QUEUE_H
#define GDBSUPPORT_JOB_QUEUE_H

#include <queue>
#include <thread>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>
#include "gdbsupport/gdb_optional.h"

namespace gdb
{

/* A job queue.

   A job queue is a thread-safe queue.  A job can be pushed on the
   queue; then other code can pop a job off the queue.  A "job" in
   this sense is just an object -- the code that is pushing and poping
   must decide what to do with the objects.
   
   Note that while this allows multiple writers, it is only designed
   with a single reader in mind.  */
template<typename T>
class job_queue
{
public:
  DISABLE_COPY_AND_ASSIGN (job_queue);

  /* Push a job on the queue.  */
  void push (T &&job)
  {
    gdb_assert (!m_shutdown);
    std::lock_guard<std::mutex> guard (m_jobs_mutex);
    m_jobs.emplace (std::move (job));
    /* If we wanted multiple readers, we'd have to notify all
       here.  */
    m_jobs_cv.notify_one ();
  }

  /* Pop a job from the queue.  This returns true if a job was found;
     the job is moved into RESULT.  Otherwise, returns false, which
     indicates that there will be no more jobs.  This blocks until
     either a job is ready, or until shutdown is called.  */
  bool pop (T &result)
  {
    std::lock_guard<std::mutex> guard (m_jobs_mutex);
    while (!m_shutdown && m_jobs.empty ())
      m_jobs.cv_wait (guard);
    if (!m_jobs.empty ())
      {
	result = std::move (m_jobs.front ());
	m_jobs.pop ();
	return true;
      }
    return false;
  }

  /* Call to shut down the queue.  This marks the end of the jobs; it
     is an error to call post after this.  */
  void shutdown ();

private:

  /* The jobs that have not been processed yet.  */
  std::queue<T> m_jobs;

  /* True after shutdown.  */
  bool m_shutdown = false;

  /* A condition variable and mutex that are used for communication
     between the main thread and the worker threads.  */
  std::condition_variable m_jobs_cv;
  std::mutex m_jobs_mutex;
};

}

#endif /* GDBSUPPORT_JOB_QUEUE_H */
