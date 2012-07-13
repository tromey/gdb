# Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

# Map inferiors to dictionaries holding the memoized strings.
_infmap = { }

# When an inferior exits, clear its entry.
def _clear_inferior_entry(event):
    global _infmap
    if event.inferior in _infmap:
        del _infmap[event.inferior]

gdb.events.exited.connect(_clear_inferior_entry)

class _MemoizeFunction(gdb.Function):
    """Memoize a string in the inferior.
    Usage: $_memoize(STRING)
    By default, GDB will call malloc for each new string created in
    the inferior.  This function memoizes such strings, so that it is
    only allocated a single time.  Note that this means that writes to
    the memoized string will be shared by all invocations."""

    def __init__(self):
        super(_MemoizeFunction, self).__init__('_memoize')

    def invoke(self, value):
        global _infmap

        inferior = gdb.selected_inferior()
        if inferior not in _infmap:
            _infmap[inferior] = {}

        strvalue = str(value)
        if strvalue not in _infmap[inferior]:
            # Coerce to the inferior to make sure we allocate it a
            # single time.
            value = value.address.dereference()
            _infmap[strvalue] = value

        return _infmap[strvalue]

_MemoizeFunction()
