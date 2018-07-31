dnl Autoconf configure script for GDB, the GNU debugger.
dnl Copyright (C) 2018 Free Software Foundation, Inc.
dnl
dnl This file is part of GDB.
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_DEFUN([AM_GDB_CXX_DEBUG],[
AC_ARG_ENABLE(cxx-debug,
AS_HELP_STRING([--enable-cxx-debug],
               [enable libstdc++ debug mode (yes/no/auto, default is auto)]),
[case "${enableval}" in
  yes| no | auto) ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-cxx-debug option) ;;
esac],[enable_cxx_debug=auto])

if test $enable_cxx_debug = auto; then
  if $development; then
    enable_cxx_debug=yes
  fi
fi
if test $enable_cxx_debug = yes; then
  AC_DEFINE(_GLIBCXX_DEBUG, 1,
            [Define if libstdc++ debug mode should be enabled])
  AC_DEFINE(_GLIBCXX_DEBUG_PEDANTIC, 1,
            [Define if libstdc++ pedantic debug mode should be enabled])
fi])
