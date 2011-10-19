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
import urllib

faq_url = 'http://sourceware.org/gdb/wiki/FAQ?action=raw'

class _FaqCommand(gdb.Command):
    """Show the FAQ.
    Usage: faq"""

    def __init__(self):
        super(_FaqCommand, self).__init__('faq', gdb.COMMAND_SUPPORT,
                                          gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        global faq_url
        f = urllib.urlopen(faq_url)
        try:
            # FIXME should use MoinMoin to parse.
            print f.read()
        finally:
            f.close()

_FaqCommand()
