# Copyright (C) 2011, 2013 Free Software Foundation, Inc.

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

ok = True
try:
    import text2html
except ImportError as err:
    ok = False

faq_url = 'http://sourceware.org/gdb/wiki/FAQ?action=print'

class _FaqCommand(gdb.Command):
    """Show the FAQ.
    Usage: faq"""

    def __init__(self):
        super(_FaqCommand, self).__init__('faq', gdb.COMMAND_SUPPORT,
                                          gdb.COMPLETE_NONE)

    def invoke(self, arg, from_tty):
        global faq_url

        try:
            import html2text
        except ImportError as err:
            raise gdb.GdbError("Could not find Python html2text package -- try 'easy_install html2text'")

        f = urllib.urlopen(faq_url)
        try:
            data = f.read().decode('utf-8')
            h = html2text.HTML2Text(baseurl = faq_url)
            h.ignore_images = True
            h.ignore_links = True
            print h.handle(data)
        finally:
            f.close()

_FaqCommand()
