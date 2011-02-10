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

class ThreadHolder:

    """A class that can be used with the 'with' statement to save and
restore the current thread while operating on some other thread."""

    def __init__(self, thread):
        self.thread = thread

    def __enter__(self):
        self.save = gdb.selected_thread()
        self.thread.switch()

    def __exit__(self, exc_type, exc_value, traceback):
        try:
            self.save.switch()
        except:
            pass
        return None
