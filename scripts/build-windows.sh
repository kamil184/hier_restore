#!/bin/sh
# Builds hier_restore.so for the Windows version of the OSS CAD Suite, on Linux with the MinGW compiler.
# Usage: scripts/build-windows.sh <path to the unpacked Windows OSS CAD Suite>
#
# On Windows, a plugin is linked against the functions yosys.exe exports. The OSS CAD Suite does not ship
# the import library for them, so it is made here from the export table of yosys.exe.
# Only the functions of yosys itself are taken: the plugin needs no others, and dlltool cannot read
# some names of the slang frontend that yosys.exe also exports.
set -e
SUITE="$1"
cd "$(dirname "$0")/.."

{
	echo "LIBRARY yosys.exe"
	echo "EXPORTS"
	x86_64-w64-mingw32-objdump -p "$SUITE/bin/yosys.exe" \
		| awk '/\[Ordinal\/Name Pointer\] Table/ { table = 1; next } table && /^\t\[/ { print $NF } table && /^$/ { table = 0 }' \
		| grep Yosys
} > yosys.def
x86_64-w64-mingw32-dlltool -d yosys.def -l yosys.exe.a

x86_64-w64-mingw32-g++ -std=c++20 -O2 -D_YOSYS_ -I"$SUITE/share/yosys/include" \
	-shared -o hier_restore.so src/hier_restore.cc yosys.exe.a
rm yosys.def yosys.exe.a
