# #####################################################################
# FSE - tester Makefile
# Copyright (C) Yann Collet 2011-2014
# GPL v2 License
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# You can contact the author at :
#  - Public forum froup : https://groups.google.com/forum/#!forum/lz4c
# #####################################################################
# fse : Command Line Utility, featuring benchmark and validation tools
# #####################################################################

RELEASE = v0.2.0

DESTDIR?=
CFLAGS ?= -O3
CFLAGS += -I.. -std=c99 -Wall -Wextra -Wundef -Wshadow -Wcast-align -Wstrict-prototypes
CF32    = -m32

# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif


default: fse

all: fse fse32 fullbench fullbench32 fuzzer fuzzer32 fuzzerU16 probagen

fse: bench.c commandline.c fileio.c xxhash.c zlibh.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

fse32: bench.c commandline.c fileio.c xxhash.c zlibh.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $(CF32) $^ -o $@$(EXT)

fuzzer: fuzzer.c xxhash.c ../fse.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

fuzzer32: fuzzer.c xxhash.c ../fse.c
	$(CC) $(CFLAGS) $(CF32) $^ -o $@$(EXT)

fuzzerU16: fuzzerU16.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

fuzzerU16_32: fuzzerU16.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $(CF32) $^ -o $@$(EXT)

fullbench: fullbench.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

fullbench32: fullbench.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $(CF32) $^ -o $@$(EXT)

probagen: probaGenerator.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

clean:
	@rm -f core *.o fse$(EXT) fse32$(EXT) fuzzer$(EXT) fuzzer32$(EXT) fuzzerU16$(EXT) fuzzerU16_32$(EXT) fullbench$(EXT) fullbench32$(EXT) probagen$(EXT)
	@echo Cleaning completed

test-fse: probagen fse
	./probagen 20%
	./fse -f proba.bin -o tmp
	./fse -df tmp -o result
	diff proba.bin result
	rm result
	rm proba.bin
	rm tmp

test: test-fse fullbench fuzzer fuzzerU16
	./fuzzer
	./fullbench -i1
	./fuzzerU16

test-fse32: probagen fse32
	./probagen 20%
	./fse32 -f proba.bin -o tmp
	./fse32 -df tmp -o result
	diff proba.bin result
	rm result
	rm tmp
	rm proba.bin

test32: test-fse32 fullbench32 fuzzer32 fuzzerU16_32
	./fuzzer32
	./fullbench32 -i1
	./fuzzerU16_32

memtest: fuzzer fullbench
	valgrind --leak-check=yes ./fuzzer -i256
	valgrind --leak-check=yes ./fullbench -i1
	valgrind --leak-check=yes ./fuzzerU16 -i128

test-all: test test32 memtest
	@echo test completed

