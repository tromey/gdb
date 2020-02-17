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

#include <condition_variable>
#include <list>
#include <mutex>
#include <queue>
#include <thread>
#include "gdbsupport/gdb_optional.h"

namespace gdb
{

/* A job queue is a thread-safe queue.  A job can be pushed on the
   queue; then other code can pop a job off the queue.  A "job" in
   this sense is just an object -- the code that is pushing and
   popping must decide what to do with the objects.

   While this allows multiple writers, it is only designed with a
   single reader in mind.

   There is no built-in support for noticing when a queue is
   finished.  The user must arrange this.  */
template<typename T>
class job_queue
{
public:
  job_queue () = default;
  ~job_queue () = default;
  DISABLE_COPY_AND_ASSIGN (job_queue);

  /* Push a job on the queue.  */
  void push (T &&job)
  {
    std::lock_guard<std::mutex> guard (m_jobs_mutex);
    m_jobs.emplace (std::move (job));
    /* If we wanted multiple readers, we'd have to notify all
       here.  */
    m_jobs_cv.notify_one ();
  }

  /* Pop a job from the queue.  This blocks until a job is ready.  */
  T pop ()
  {
    std::unique_lock<std::mutex> guard (m_jobs_mutex);
    while (m_jobs.empty ())
      m_jobs_cv.wait (guard);
    T result = std::move (m_jobs.front ());
    m_jobs.pop ();
    return result;
  }

private:

  /* The jobs that have not been processed yet.  */
  std::queue<T, std::list<T>> m_jobs;

  /* A condition variable and mutex that are used for communication
     between the main thread and the worker threads.  */
  std::condition_variable m_jobs_cv;
  std::mutex m_jobs_mutex;
};

}

#endif /* GDBSUPPORT_JOB_QUEUE_H */
