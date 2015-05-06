# Copyright (C) 2010-2015 Free Software Foundation, Inc.

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

# This file is part of the GDB testsuite.  It tests python's find_source_hook.
import gdb

# This gets set by the test script.
new_path = None

# Number of calls to do_nothing
do_nothing_calls = 0

# Number of calls to new_path
new_path_calls = 0

def find_source_do_nothing (source):
    global do_nothing_calls
    do_nothing_calls += 1

def find_source_raise (source):
    raise Exception("oops")

def find_source_bad_return (source):
    return 1

def find_source_new_path (source):
    global new_path_calls
    new_path_calls += 1
    global new_path
    return new_path
