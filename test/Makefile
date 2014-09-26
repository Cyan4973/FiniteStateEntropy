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

RELEASE=v0.1.1

DESTDIR?=
CC     := $(CC)
CFLAGS ?= -O3
CFLAGS += -I.. -std=c99 -Wall -Wextra -Wundef -Wshadow -Wstrict-prototypes
CF32    = -m32 -march=pentiumpro

# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif


default: fse

all: fse fse32 fuzzer fuzzer32 probagen

fse: bench.c commandline.c fileio.c lz4hce.c xxhash.c fseDist.c zlibh.c renorm.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

fse32: bench.c commandline.c fileio.c lz4hce.c xxhash.c fseDist.c zlibh.c renorm.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $(CF32) $^ -o $@$(EXT)

fuzzer: fuzzer.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

fuzzer32: fuzzer.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $(CF32) $^ -o $@$(EXT)

fullbench: fullbench.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

fullbench32: fullbench.c xxhash.c ../fse.c ../fseU16.c
	$(CC) $(CFLAGS) $(CF32) $^ -o $@$(EXT)

probagen: probaGenerator.c
	$(CC) $(CFLAGS) $^ -o $@$(EXT)

clean:
	@rm -f core *.o fse$(EXT) fse32$(EXT) fuzzer$(EXT) fuzzer32$(EXT) fullbench$(EXT) fullbench32$(EXT) probagen$(EXT)
	@echo Cleaning completed

test: fullbench fullbench32 fuzzer fuzzer32
	./fullbench -i1
	./fullbench32 -i1
	./fuzzer
	./fuzzer32

memtest: fuzzer fullbench
	valgrind ./fuzzer -i200
	valgrind ./fullbench -i1

test-all: test memtest
	@echo test completed

