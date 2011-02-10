# Thread utilities.
# Copyright (C) 2010, 2011 Free Software Foundation, Inc.

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import gdb
import gdb.thread

def _print_thread(thr, owner):
    "A helper function to nicely print a gdb.Thread."
    if thr == selected_thread:
        print "* ",
    else:
        print "  ",
    if owner:
        print "Owned by thread",
    else:
        print "Thread",
    (pid, lwp, tid) = thr.ptid
    print "%d  " % lwp
    # FIXME - function, file name, etc

# Map a mutex address to its owning thread.
_mutex_map = {}

# Map a thread address to a set of mutexes it owns.
_thread_map = {}

class _mutex_acquired_probe(gdb.Breakpoint):
    def __init__(self):
        super(_mutex_probe, self).__init__('probe:pthread:mutex_acquired')
        # mutex_acquired
        # mutex_block  # misnamed
        # mutex_release

    def eval(self):
        global _thread_map
        global _mutex_map
        thread = gdb.selected_thread()
        mutex = gdb.parse_and_eval('$_marker_arg0')
        if thread not in _thread_map:
            _thread_map[thread] = {}
        _thread_map[thread][mutex] = 1
        _mutex_map[mutex] = thread
        # Keep going.
        return False

class _mutex_released_probe(gdb.Breakpoint):
    def __init__(self):
        super(_mutex_probe, self).__init__('probe:pthread:mutex_released')

class InfoMutex(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "info mutex", gdb.COMMAND_NONE)

    def invoke(self, arg, from_tty):
        # Map a mutex ID to the LWP owning the mutex.
        owner = {}
        # Map an LWP id to a thread object.
        threads = {}
        # Map a mutex ID to a list of thread objects that are waiting
        # for the lock.
        mutexes = {}

        for inf in gdb.inferiors():
            for thr in inf.threads():
                id = thr.ptid[1]
                threads[id] = thr
                with ThreadHolder(thr):
                    frame = gdb.selected_frame()
                    lock_name = None
                    for n in range(5):
                        if frame is None:
                            break
                        fn_sym = frame.function()
                        if fn_sym is not None and (fn_sym.name == '__pthread_mutex_lock' or fn_sym.name == '__pthread_mutex_lock_full' or fn_sym.name == 'pthread_mutex_timedlock'):
                            m = frame.read_var('mutex')
                            lock_name = long(m)
                            if lock_name not in owner:
                                owner[lock_name] = long(m['__data']['__owner'])
                                break
                        frame = frame.older()
                    if lock_name not in mutexes:
                        mutexes[lock_name] = []
                    mutexes[lock_name] += [thr]

        selected_thread = gdb.selected_thread()

        for id in mutexes.keys():
            if id is None:
                print "Threads not waiting for a lock:"
            else:
                print "Mutex 0x%x:" % id
                _print_thread(threads[owner[id]], True)
            for thr in mutexes[id]:
                _print_thread(thr, False)
            print

InfoMutex()
