/* Thread-related for GDB.

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
#include "common/host-thread.h"
#include "event-loop.h"
#include "ser-event.h"
#include <mutex>

/* True for the main thread, false otherwise.  */

static thread_local bool main_thread;

/* The serial event used when posting runnables.  */

static struct serial_event *runnable_event;

/* Runnables that have been posted.  */

static std::vector<std::unique_ptr<runnable>> runnables;

/* Mutex to hold when handling runnable_event or runnables.  */

std::mutex runnable_mutex;

/* See gdb_thread.h.  */

void
this_is_the_main_thread ()
{
  main_thread = true;
}

/* See gdb_thread.h.  */

bool
main_thread_p ()
{
  return main_thread;
}

/* Run all the queued runnables.  */

static void
run_events (int error, gdb_client_data client_data)
{
  std::vector<std::unique_ptr<runnable>> local;

  /* Hold the lock while changing the globals, but not while running
     the runnables.  */
  {
    std::lock_guard<std::mutex> lock (runnable_mutex);

    /* Clear the event fd.  Do this before flushing the events list,
       so that any new event post afterwards is sure to re-awaken the
       event loop.  */
    serial_event_clear (runnable_event);

    /* Move the vector in case running a runnable pushes a new
       runnable.  */
    std::swap (local, runnables);
  }

  for (auto &item : local)
    (*item) ();
}

/* See gdb_thread.h.  */

void
run_on_main_thread (std::unique_ptr<runnable> &&r)
{
  std::lock_guard<std::mutex> lock (runnable_mutex);
  runnables.emplace_back (std::move (r));
  serial_event_set (runnable_event);
}

void
_initialize_gdb_thread ()
{
  runnable_event = make_serial_event ();
  add_file_handler (serial_event_fd (runnable_event), run_events, nullptr);
}
