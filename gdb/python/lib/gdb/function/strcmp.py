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

class _StrcmpFunction(gdb.Function):
    """Like C's strcmp, but runs in GDB, not in the inferior.  This
    makes it somewhat faster.
    Usage: $_strcmp(STR1, STR2, [LEN])
    LEN is an optional argument which, if given, makes this function
    act like strncmp."""

    def __init__(self):
        super(_StrcmpFunction, self).__init__('_strcmp')

    def invoke(self, p1, p2, max_len = False):
        if max_len is not False:
            str1 = p1.string(length = max_len)
            str2 = p2.string(length = max_len)
        else:
            str1 = p1.string()
            str2 = p2.string()
        return cmp(str1, str2)

_StrcmpFunction()
