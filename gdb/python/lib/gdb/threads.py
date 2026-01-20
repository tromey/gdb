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

"""Utilities for working with threads."""

import gdb

# The current providers.
_providers = []


# This should only really be used by gdb.command.threads.
# It is intentionally not documented.
def get_providers():
    return _providers


def add_green_thread_provider(prov):
    """Register a green-thread provider."""
    assert isinstance(prov.name, str)
    assert prov.name not in [x.name for x in _providers]
    _providers.append(prov)


def new_green_thread(prov, tid: int, callback):
    """Create a new green thread.

    PROV is the green thread provider.
    TID is the thread ID, an integer.
    CALLBACK implements the needed callbacks used by the green thread
    implementation."""
    result = gdb.create_green_thread(tid, callback)
    result.provider = prov
    return result


def current_green_thread():
    """Return the active green thread for the selected thread.

    This will return the green thread currently scheduled on the
    currently selected thread, or None.
    """
    assert not isinstance(gdb.selected_thread(), gdb.GreenThread)
    for prov in _providers:
        result = prov.current_green_thread()
        if result is not None:
            return result
    return None
