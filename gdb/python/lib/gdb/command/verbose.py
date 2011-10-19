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

class _VerboseCmd(gdb.Command):
    """Run a command verbosely.
    Usage: verbose COMMAND [ARGS]
    This runs the given command with 'set verbose on' temporarily in effect."""

    def __init__(self, name):
        super(_VerboseCmd, self).__init__(name, gdb.COMMAND_SUPPORT)

    def invoke(self, arg, from_tty):
        if gdb.parameter('verbose'):
            save = 'on'
        else:
            save = 'off'
        gdb.execute('set verbose on', to_string = True)
        try:
            gdb.execute(arg, from_tty)
        finally:
            gdb.execute('set verbose ' + save, to_string = True)

_VerboseCmd('verbose')
