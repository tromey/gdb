#!/bin/sh

# Build gdb as appropriate for rust.

srcdir="$1"
builddir="$2"
installdir="$3"

set -e

mkdir -p "$builddir"
mkdir -p "$installdir"

cd "$builddir"
# If Makefile already exists, just use it.  It will handle re-running
# config.status as needed, whereas re-running configure will
# needlessly cause most objects to be rebuilt.  Also, incremental
# builds typically are fine in gdb.
if ! test -f Makefile; then
    "$srcdir"/configure \
	     CFLAGS=-O2 CXXFLAGS=-O2 \
	     --prefix="$installdir" \
	     --with-separate-debug-dir=/usr/lib/debug \
	     --disable-binutils \
	     --disable-gas \
	     --disable-gdbserver \
	     --disable-gold \
	     --disable-gprof \
	     --disable-ld \
	     --without-guile \
	     --with-python=python2 \
	     --enable-targets=all \
	     --enable-64-bit-bfd
fi
make
make install
