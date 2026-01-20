# Threading utilities.
# Copyright (C) 2025, 2026 Free Software Foundation, Inc.

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

"""GDB commands for working with threads."""

import gdb
from gdb.threads import get_providers


class _enable_providers(gdb.Command):
    """
    Enable one or all green thread providers.
    Usage: enable green-thread-providers [NAME]
    """

    def __init__(self):
        super().__init__("enable green-thread-providers", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        if args is None:
            providers = get_providers()
        else:
            providers = [x for x in get_providers() if x.name == args]
        for prov in providers:
            if not prov.enabled:
                prov.enable()

    def complete(self, text, word):
        result = []
        for prov in get_providers():
            if prov.name.startswith(word):
                result.append(prov.name)
        return result


class _disable_providers(gdb.Command):
    """
    Disable one or all green thread providers.
    Usage: disable green-thread-providers [NAME]
    """

    def __init__(self):
        super().__init__("disable green-thread-providers", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        if args is None:
            providers = get_providers()
        else:
            providers = [x for x in get_providers() if x.name == args]
        for prov in providers:
            if prov.enabled:
                prov.disable()
        for inf in gdb.inferiors():
            for thread in inf.threads():
                if isinstance(thread, gdb.GreenThread) and thread.provider in providers:
                    thread.set_exit()

    def complete(self, text, word):
        result = []
        for prov in get_providers():
            if prov.name.startswith(word):
                result.append(prov.name)
        return result


class _info_providers(gdb.Command):
    """
    List all green thread providers.
    """

    def __init__(self):
        super().__init__("info green-thread-providers", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        if len(get_providers()) == 0:
            print("No green thread providers registered.")
            return

        fmt = " {:<3s}  {:s}"
        print(fmt.format("Enb", "Name"))
        for prov in get_providers():
            if prov.enabled:
                enb = "y"
            else:
                enb = "n"
            print(fmt.format(enb, prov.name))


_enable_providers()
_disable_providers()
_info_providers()
