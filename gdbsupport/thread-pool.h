/* Thread pool

   Copyright (C) 2019-2024 Free Software Foundation, Inc.

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

#ifndef GDBSUPPORT_THREAD_POOL_H
#define GDBSUPPORT_THREAD_POOL_H

#include <queue>
#include <vector>
#include <functional>
#include <chrono>
#if CXX_STD_THREAD
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#endif
#include <optional>

namespace gdb
{

#if CXX_STD_THREAD

/* Simply use the standard future.  */
template<typename T>
using future = std::future<T>;

/* ... and the standard future_status.  */
using future_status = std::future_status;

#else /* CXX_STD_THREAD */

/* A compatibility enum for std::future_status.  This is just the
   subset needed by gdb.  */
enum class future_status
{
  ready,
  timeout,
};

/* A compatibility wrapper for std::future.  Once <thread> and
   <future> are available in all GCC builds -- should that ever happen
   -- this can be removed.  GCC does not implement threading for
   MinGW, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=93687.

   Meanwhile, in this mode, there are no threads.  Tasks submitted to
   the thread pool are invoked immediately and their result is stored
   here.  The base template here simply wraps a T and provides some
   std::future compatibility methods.  The provided methods are chosen
   based on what GDB needs presently.  */

template<typename T>
class future
{
public:

  explicit future (T value)
    : m_value (std::move (value))
  {
  }

  future () = default;
  future (future &&other) = default;
  future (const future &other) = delete;
  future &operator= (future &&other) = default;
  future &operator= (const future &other) = delete;

  void wait () const { }

  template<class Rep, class Period>
  future_status wait_for (const std::chrono::duration<Rep,Period> &duration)
    const
  {
    return future_status::ready;
  }

  T get () { return std::move (m_value); }

private:

  T m_value;
};

/* A specialization for void.  */

template<>
class future<void>
{
public:
  void wait () const { }

  template<class Rep, class Period>
  future_status wait_for (const std::chrono::duration<Rep,Period> &duration)
    const
  {
    return future_status::ready;
  }

  void get () { }
};

#endif /* CXX_STD_THREAD */


/* A thread pool.

   There is a single global thread pool, see g_thread_pool.  Tasks can
   be submitted to the thread pool.  They will be processed in worker
   threads as time allows.  */
class thread_pool
{
public:
  /* The sole global thread pool.  */
  static thread_pool *g_thread_pool;

  ~thread_pool ();
  DISABLE_COPY_AND_ASSIGN (thread_pool);

  /* Set the thread count of this thread pool.  By default, no threads
     are created -- the thread count must be set first.  */
  void set_thread_count (size_t num_threads);

  /* Return the number of executing threads.  */
  size_t thread_count () const
  {
#if CXX_STD_THREAD
    return m_thread_count;
#else
    return 0;
#endif
  }

  /* A priority that can be used for tasks that will be immediately
     waited for.  This cannot be used by ordinary code; it's reserved
     for shutting down threads.  */
  static constexpr size_t BLOCKING_PRIORITY = (size_t) -1;

  /* The default priority.  Code of course should not rely on this
     exact value; but it's fine to raise or lower the priority of a
     task by adding a constant to this.  */
  static constexpr size_t DEFAULT_PRIORITY = 1000;

  /* The lowest priority.  */
  static constexpr size_t LOW_PRIORITY = 0;

  /* Post a task to the thread pool.  A future is returned, which can
     be used to wait for the result.  */
  future<void> post_task (std::function<void ()> &&func,
			  size_t prio = DEFAULT_PRIORITY)
  {
    gdb_assert (prio != BLOCKING_PRIORITY);
#if CXX_STD_THREAD
    std::packaged_task<void ()> task (std::move (func));
    future<void> result = task.get_future ();
    do_post_task (std::packaged_task<void ()> (std::move (task)), prio);
    return result;
#else
    func ();
    return {};
#endif /* CXX_STD_THREAD */
  }

  /* Post a task to the thread pool.  A future is returned, which can
     be used to wait for the result.  */
  template<typename T>
  future<T> post_task (std::function<T ()> &&func,
		       size_t prio = DEFAULT_PRIORITY)
  {
    gdb_assert (prio != BLOCKING_PRIORITY);
#if CXX_STD_THREAD
    std::packaged_task<T ()> task (std::move (func));
    future<T> result = task.get_future ();
    do_post_task (std::packaged_task<void ()> (std::move (task)), prio);
    return result;
#else
    return future<T> (func ());
#endif /* CXX_STD_THREAD */
  }

private:

  thread_pool () = default;

#if CXX_STD_THREAD
  /* The callback for each worker thread.  */
  void thread_function ();

  /* Post a task to the thread pool.  A future is returned, which can
     be used to wait for the result.  */
  void do_post_task (std::packaged_task<void ()> &&func, size_t prio);

  /* The current thread count.  */
  size_t m_thread_count = 0;

  /* A convenience typedef for the type of a task.  */
  typedef std::packaged_task<void ()> task_t;

  /* A convenience typedef for the element type of the priority queue.
     The first element of the pair is the priority.  An optional is
     used to represent a task.  If the optional is empty, then this
     means that the receiving thread should terminate.  If the
     optional is non-empty, then it is an actual task to evaluate.  */
  typedef std::pair<size_t, std::optional<task_t>> element_t;

  /* A comparison function for queue elements.  */
  struct queue_compare
  {
    bool operator() (const element_t &a, const element_t &b) const
    {
      return a.first < b.first;
    }
  };

  /* A subclass of priority_queue so that we can extract just the
     highest-priority task.  This is needed because
     priority_queue::top returns a const reference.  */
  class queue : public std::priority_queue<element_t,
					   std::vector<element_t>,
					   queue_compare>
  {
  public:

    std::optional<task_t> pop_task ()
    {
      std::pop_heap (c.begin (), c.end (), comp);
      std::optional<task_t> result = std::move (c.back ().second);
      c.pop_back ();
      return result;
    }
  };

  /* The tasks that have not been processed yet.*/
  queue m_tasks;

  /* A condition variable and mutex that are used for communication
     between the main thread and the worker threads.  */
  std::condition_variable m_tasks_cv;
  std::mutex m_tasks_mutex;
  bool m_sized_at_least_once = false;
#endif /* CXX_STD_THREAD */
};

}

#endif /* GDBSUPPORT_THREAD_POOL_H */
