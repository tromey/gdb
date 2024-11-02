#!/usr/bin/env python3

# gdbarch checker.
#
# Copyright (C) 2024 Free Software Foundation, Inc.
#
# This file is part of GDB.
#
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

import fileinput
import glob
import re
import sys

# gdbarch_components is imported only for its side-effect of filling
# `gdbarch_types.components`.
import gdbarch_components  # noqa: F401 # type: ignore
from gdbarch_types import Component, Function, Info, Value, components


def not_info(c: Component):
    "Filter function to omit Info components."
    return type(c) is not Info


if len(sys.argv) != 1:
    # Must be run in gdb srcdir.
    print("usage: check-gdbarch.py")
    sys.exit(1)


# Make a hash holding all the gdbarch customization names.
defined_names = set()
set_names = set()
called_names = set()
for c in filter(not_info, components):
    if c.implement:
        defined_names.add(c.name)
    if c.predicate:
        # Predicates are always "set".
        pname = c.name + "_p"
        set_names.add(pname)
        defined_names.add(pname)


def find_local_files():
    result = []
    for name in glob.glob("*.[chyl]"):
        if "gdbarch" not in name:
            result.append(name)
    return result


files = find_local_files()
files.extend(glob.glob("*/*.[ch]"))

# FIXME could keep counts here and then look for deletion
# opportunities.
rx = re.compile(r"\b(set_)?gdbarch_([a-zA-Z0-9_]+)\b")
for line in fileinput.input(files=files):
    m = rx.search(line)
    if m:
        if m[0] == "gdbarch_p":
            # There are a few variables with this name, exclude them.
            pass
        elif m[1]:
            set_names.add(m[2])
        else:
            called_names.add(m[2])


for elt in defined_names - set_names:
    print(f"never set: {elt}")
for elt in defined_names - called_names:
    # Don't report _p functions here, predicate=True is sometimes used
    # to allow optional functions.
    if not elt.endswith("_p"):
        print(f"never called: {elt}")
