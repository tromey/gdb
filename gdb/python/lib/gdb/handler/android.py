# Copyright (C) 2026 Free Software Foundation, Inc.

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

# Missing objfile handler for Android.

import gdb
import gdb.missing_objfile


class _AndroidHandler(gdb.missing_objfile.MissingObjfileHandler):
    buildid_required = False

    def __call__(self, pspace, buildid, filename):
        elts = filename.split(".apk!/", maxsplit=1)
        if len(elts) != 2:
            return None
        initial, member = elts
        apk_fd = gdb.TargetFile(initial + ".apk")

        import zipfile

        apk_file = zipfile.ZipFile(apk_fd)
        info = apk_file.getinfo(member)
        member_fd = apk_file.open(info)

        # Use the full filename here so the user can see what is going
        # on.
        return gdb.missing_objfile.BFDFile(member_fd, filename)


gdb.missing_objfile.register_handler(None, _AndroidHandler("Android"))
