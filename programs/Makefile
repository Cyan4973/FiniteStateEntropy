# #####################################################################
# FSE - tester Makefile
# Copyright (C) Yann Collet 2013-present
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
# fuzzer : stress tester for FSE library
# fuzzerHuff0 : stress tester for Huff0 library
# #####################################################################

RELEASE    = v0.3.4
FSEDIR     = ../lib

ifeq ($(shell $(CC) -v 2>&1 | grep -c "gcc version "), 1)
ALIGN_LOOP = # -falign-functions=16 # -falign-loops=32
else
ALIGN_LOOP =
endif

DESTDIR   ?=
CPPFLAGS  += -I$(FSEDIR)
CFLAGS    ?= -O3 $(ALIGN_LOOP)
CFLAGS    += -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow \
             -Wstrict-aliasing=1 -Wswitch-enum -Wstrict-prototypes -Wundef
FLAGS      = $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(MOREFLAGS)
FSETEST   ?=
FSEU16TEST?= $(FSETEST)

# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif


.PHONY: default
default: fse

.PHONY: all
all: fse fullbench fuzzer fuzzerU16 fuzzerHuff0 probagen

.PHONY: all32
all32: fse32 fullbench32 fuzzer32 fuzzerU16_32 fuzzerHuff0_32

fse32: CFLAGS += -m32
fsex32: CFLAGS += -mx32
fse fse32 fsex32: bench.c commandline.c fileio.c xxhash.c zlibh.c \
                  $(FSEDIR)/entropy_common.c $(FSEDIR)/hist.c \
                  $(FSEDIR)/fse_decompress.c $(FSEDIR)/fse_compress.c $(FSEDIR)/fseU16.c \
                  $(FSEDIR)/huf_compress.c $(FSEDIR)/huf_decompress.c
	$(CC) $(FLAGS) $^ -o $@$(EXT)

fse_opt:
	$(MAKE) fse MOREFLAGS=-flto

fuzzer fuzzer32: CPPFLAGS += -DDEBUGLEVEL=1
fuzzer32: CFLAGS += -m32
fuzzer fuzzer32: fuzzer.c xxhash.c $(FSEDIR)/entropy_common.c $(FSEDIR)/hist.c \
                 $(FSEDIR)/fse_decompress.c $(FSEDIR)/fse_compress.c
	$(CC) $(FLAGS) $^ -o $@$(EXT)

fuzzerU16 fuzzerU16_32: CPPFLAGS += -DDEBUGLEVEL=1
fuzzerU16_32: CFLAGS += -m32
fuzzerU16 fuzzerU16_32: fuzzerU16.c xxhash.c $(FSEDIR)/entropy_common.c $(FSEDIR)/hist.c \
                        $(FSEDIR)/fse_decompress.c $(FSEDIR)/fse_compress.c $(FSEDIR)/fseU16.c
	$(CC) $(FLAGS) $^ -o $@$(EXT)

fuzzerHuff0 fuzzerHuff0_32: CPPFLAGS += -DDEBUGLEVEL=1
fuzzerHuff0_32: CFLAGS += -m32
fuzzerHuff0 fuzzerHuff0_32: fuzzerHuff0.c xxhash.c $(FSEDIR)/entropy_common.c $(FSEDIR)/hist.c \
                            $(FSEDIR)/huf_compress.c $(FSEDIR)/huf_decompress.c \
                            $(FSEDIR)/fse_decompress.c $(FSEDIR)/fse_compress.c
	$(CC) $(FLAGS) $^ -o $@$(EXT)

fullbench32: CFLAGS += -m32
fullbenchx32: CFLAGS += -mx32
fullbench fullbench32 fullbenchx32: fullbench.c xxhash.c $(FSEDIR)/entropy_common.c $(FSEDIR)/hist.c \
                                    $(FSEDIR)/fse_decompress.c $(FSEDIR)/fse_compress.c $(FSEDIR)/fseU16.c \
                                    $(FSEDIR)/huf_compress.c $(FSEDIR)/huf_decompress.c
	$(CC) $(FLAGS) $^ -o $@$(EXT)

probagen: probaGenerator.c
	$(CC) $(FLAGS) $^ -o $@$(EXT)

.PHONY: clean
clean:
	@$(RM) fse$(EXT) fse32$(EXT) fsex32$(EXT) fullbench$(EXT) fullbench32$(EXT) fullbenchx32$(EXT) probagen$(EXT)
	@$(RM) fuzzer$(EXT) fuzzer32$(EXT) fuzzerU16$(EXT) fuzzerU16_32$(EXT) fuzzerHuff0$(EXT) fuzzerHuff0_32$(EXT)
	@$(RM) core *.o tmp result proba.bin
	@echo Cleaning completed


.PHONY: list
list:
	@$(MAKE) -pRrq -f $(lastword $(MAKEFILE_LIST)) : 2>/dev/null | awk -v RS= -F: '/^# File/,/^# Finished Make data base/ {if ($$1 !~ "^[#.]") {print $$1}}' | sort | egrep -v -e '^[^[:alnum:]]' -e '^$@$$' | xargs

test-fse: probagen fse
	./probagen 20%
	@echo "**** compress using FSE ****"
	./fse -f proba.bin tmp
	./fse -df tmp result
	diff proba.bin result
	@echo "**** compress using HUF ****"
	./fse -fh proba.bin tmp
	./fse -df tmp result
	diff proba.bin result
	@echo "**** compress using zlibh ****"
	./fse -fz proba.bin tmp
	./fse -df tmp result
	diff proba.bin result
	rm result
	rm proba.bin
	rm tmp

check: test-fse

test: test-fse fullbench fuzzer fuzzerU16 fuzzerHuff0
	./fuzzer $(FSETEST)
	./fullbench -i1
	./fuzzerU16 $(FSEU16TEST)
	./fuzzerHuff0 $(FSETEST)

test-fse32: probagen fse32
	./probagen 20%
	@echo compress using FSE
	./fse32 -f proba.bin tmp
	./fse32 -df tmp result
	diff proba.bin result
	@echo compress using HUF
	./fse32 -fh proba.bin tmp
	./fse32 -df tmp result
	diff proba.bin result
	@echo compress using zlibh
	./fse32 -fz proba.bin tmp
	./fse32 -df tmp result
	diff proba.bin result
	rm result
	rm tmp
	rm proba.bin

test32: test-fse32 fullbench32 fuzzer32 fuzzerU16_32 fuzzerHuff0_32
	./fuzzer32 $(FSETEST)
	./fullbench32 -i1
	./fuzzerU16_32 $(FSEU16TEST)
	./fuzzerHuff0_32 $(FSETEST)

memtest: fuzzer fullbench fuzzerU16 fuzzerHuff0
	valgrind --version
	valgrind --leak-check=yes --error-exitcode=1 ./fuzzer -i256
	valgrind --leak-check=yes --error-exitcode=1 ./fullbench -i1
	valgrind --leak-check=yes --error-exitcode=1 ./fuzzerU16 -i128
	valgrind --leak-check=yes --error-exitcode=1 ./fuzzerHuff0 -i256

test-all: test test32 memtest
	@echo test completed
