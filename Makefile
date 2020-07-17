# #####################################################################
# FSE - Makefile
# Copyright (C) Yann Collet 2015 - present
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
# This is just a launcher for the Makefile within `programs` directory
# #####################################################################

PROGDIR?= programs

.PHONY: default
default: fse check

.PHONY: fse   # only progdir knows if fse must be rebuilt or not
fse:
	$(MAKE) -C $(PROGDIR) $@
	cp $(PROGDIR)/fse .

.PHONY: check
check: fse
	$(MAKE) -C $(PROGDIR) $@

.PHONY: all test clean
all test clean:
	$(MAKE) -C $(PROGDIR) $@


.PHONY: max13test
max13test: clean
	@echo ---- test FSE_MAX_MEMORY_USAGE = 13 ----
	CPPFLAGS="-DFSE_MAX_MEMORY_USAGE=13" $(MAKE) check

.PHONY: gpptest
gpptest: clean
	@echo ---- test g++ compilation ----
	$(MAKE) -C $(PROGDIR) all CC=g++ CFLAGS="-O3 -Wall -Wextra -Wundef -Wshadow -Wcast-align -Wcast-qual -Werror"

.PHONY: armtest
armtest: clean
	@echo ---- test ARM compilation ----
	CFLAGS="-O2 -Werror" $(MAKE) -C $(PROGDIR) all CC=arm-linux-gnueabi-gcc

.PHONY: clangtest
clangtest: clean
	@echo ---- test clang compilation ----
	CFLAGS="-O3 -Werror -Wconversion -Wno-sign-conversion" CC=clang $(MAKE) -C $(PROGDIR) all

.PHONY: clangpptest
clangpptest: clean
	@echo ---- test clang++ compilation ----
	$(MAKE) -C $(PROGDIR) all CC=clang++ CFLAGS="-O3 -Wall -Wextra -Wundef -Wshadow -Wcast-align -Wcast-qual -x c++ -Werror"

.PHONY: staticAnalyze
staticAnalyze: clean
	@echo ---- static analyzer - scan-build ----
	scan-build --status-bugs -v $(MAKE) -C $(PROGDIR) all CFLAGS=-g   # does not work well; too many false positives

.PHONY: sanitize
sanitize: clean
	@echo ---- check undefined behavior and address overflows ----
	CC=clang CFLAGS="-g -O2 -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer" $(MAKE) -C $(PROGDIR) test   FSETEST="-i5000" FSEU16TEST=-i2000
	# CC=clang CFLAGS="-g -O3 -fsanitize=undefined -fsanitize=address" $(MAKE) -C $(PROGDIR) test32 FSETEST="-i5000" FSEU16TEST=-i2000  # sanitizer for 32-bits not correctly shipped with clang 3.8, which is used in travisCI
