# #####################################################################
# FSE - Makefile
# Copyright (C) Yann Collet 2015
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
# This is just a launcher for the Makefile within test directory
# #####################################################################

PROGDIR?= test

.PHONY: clean test

default: test

all:
	@cd $(PROGDIR); $(MAKE) all

test:
	@cd $(PROGDIR); $(MAKE) test

clean:
	@cd $(PROGDIR); $(MAKE) clean

test-all: clean
	@cd $(PROGDIR); $(MAKE) test-all

gpptest: clean
	@echo ---- test g++ compilation ----
	@cd $(PROGDIR); $(MAKE) all CC=g++ CFLAGS="-O3 -Wall -Wextra -Wundef -Wshadow -Wcast-align -Werror"

armtest: clean
	@echo ---- test ARM compilation ----
	@cd $(PROGDIR); $(MAKE) allNative CC=arm-linux-gnueabi-gcc MOREFLAGS="-Werror"

clangtest: clean
	@echo ---- test clang compilation ----
	@cd $(PROGDIR); $(MAKE) all CC=clang MOREFLAGS="-Werror -Wconversion -Wno-sign-conversion"

staticAnalyze: clean
	@echo ---- static analyzer - scan-build ----
	@cd $(PROGDIR); scan-build --status-bugs -v $(MAKE) all CFLAGS=-g   # does not work well; too many false positives

sanitize: clean
	@echo ---- check undefined behavior - sanitize ----
	@cd $(PROGDIR); $(MAKE) test   CC=clang MOREFLAGS="-g -fsanitize=undefined" FSETEST="-i5000" FSEU16TEST=-i2000
	@cd $(PROGDIR); $(MAKE) test32 CC=clang MOREFLAGS="-g -fsanitize=undefined" FSETEST="-i5000" FSEU16TEST=-i2000


