#!/usr/bin/env python3

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

# This is intended to be run from pre-commit.  You can also run it by
# hand by passing repository-relative filenames to it, like:
#   ./gdb/check-include-guards.py gdb/*.h


import glob
import re
import sys

status = 0

DEF = re.compile("^#ifndef ([A-Za-z0-9_]+)\n")
OLDDEF = re.compile("^#if !defined *\\(([A-Za-z0-9_]+)\\)\n")


def failure(filename, ndx, text):
    print(filename + ":" + str(ndx + 1) + ": " + text)
    global status
    status = 1


def headers(dirname):
    return glob.iglob(dirname + "/*.h")


def skip_comments_and_blanks(ndx, contents):
    while ndx < len(contents) and contents[ndx].startswith("/*"):
        while ndx < len(contents):
            ndx += 1
            if contents[ndx - 1].endswith("*/\n"):
                break
        # Skip blank lines.
        while ndx < len(contents):
            if contents[ndx].strip() != "":
                break
            ndx += 1
    return ndx


def write_header(filename, contents):
    with open(filename, "w") as f:
        f.writelines(contents)


def check_header(filename):
    # Turn x/y-z.h into X_Y_Z_H.
    assert filename.endswith(".h")
    expected = filename.replace("-", "_")
    expected = expected.replace(".", "_")
    expected = expected.replace("/", "_")
    expected = expected.upper()
    with open(filename) as f:
        contents = list(f)
    if "THIS FILE IS GENERATED" in contents[0]:
        # Ignore.
        return
    if not contents[0].startswith("/*"):
        failure(filename, 0, "header should start with comment")
        return
    i = skip_comments_and_blanks(0, contents)
    if i == len(contents):
        failure(filename, i, "unterminated intro comment or missing body")
        return
    m = DEF.match(contents[i])
    force_rewrite = False
    if not m:
        m = OLDDEF.match(contents[i])
        if not m:
            failure(filename, i, "no header guard")
            return
        force_rewrite = True
    symbol = m.group(1)
    updated = False
    if symbol != expected:
        failure(filename, i, "symbol should be: " + expected)
        force_rewrite = True
    if force_rewrite:
        contents[i] = "#ifndef " + expected + "\n"
        updated = True
    i += 1
    if i == len(contents):
        failure(filename, i, "premature EOF")
        return
    if not contents[i].startswith("#define "):
        failure(filename, i, "no define of header guard")
        return
    if contents[i] != "#define " + expected + "\n":
        failure(filename, i, "wrong symbol name in define")
        contents[i] = "#define " + expected + "\n"
        updated = True
    i = len(contents) - 1
    if not contents[i].startswith("#endif"):
        failure(filename, i, "no trailing endif")
        return
    if contents[i] != "#endif /* " + expected + " */\n":
        failure(filename, i, "incorrect endif")
        contents[i] = "#endif /* " + expected + " */\n"
        updated = True
    if updated:
        write_header(filename, contents)


for filename in sys.argv[1:]:
    check_header(filename)

sys.exit(status)
