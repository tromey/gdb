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

#ifndef COMMON_TASK_POOL_H
#define COMMON_TASK_POOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

/* A task is a wrapper for a std::function that also can be used in a
   task pool.  A task keeps track of whether it has been started, and
   so can also be called synchronously in the current thread if need
   be.  Tasks have no result; they must arrange with their creator to
   pass a result by other means.  */
class task_pool
{
public:

  explicit task_pool (unsigned int n_threads)
    : m_n_threads (n_threads)
  {
  }

  class task;
  std::shared_ptr<task> add_task (std::string &&name,
				  std::function<void ()> &&func,
				  size_t priority);

  void run (std::shared_ptr<task> &job);

private:

  DISABLE_COPY_AND_ASSIGN (task_pool);

  struct compare_tasks
  {
    bool operator() (const std::shared_ptr<task> &,
		     const std::shared_ptr<task> &);
  };

  void worker ();
  void start ();

  bool m_started = false;
  unsigned int m_n_threads;

  std::mutex m_lock;
  std::condition_variable m_var;

  std::priority_queue<std::shared_ptr<task>,
		      std::vector<std::shared_ptr<task>>,
		      compare_tasks> m_queue;
};

#endif /* COMMON_TASK_POOL_H */
