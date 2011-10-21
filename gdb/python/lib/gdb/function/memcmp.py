# Copyright (C) 2011 Free Software Foundation, Inc.

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

class _MemcmpFunction(gdb.Function):
    """Like C's memcmp, but runs in GDB, not in the inferior.  This
    makes it somewhat faster.
    Usage: $_memcmp(STR1, STR2, LEN)"""

    def __init__(self):
        super(_MemcmpFunction, self).__init__('_memcmp')

    def invoke(self, p1, p2, length):
        inf = gdb.selected_inferior()
        m1 = inf.read_memory(p1, length)
        m2 = inf.read_memory(p2, length)
        return cmp(m1, m2)

_MemcmpFunction()
