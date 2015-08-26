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
	$(MAKE) -C $(PROGDIR) all

test:
	$(MAKE) -C $(PROGDIR) test

clean:
	$(MAKE) -C $(PROGDIR) clean

gpptest: clean
	@echo ---- test g++ compilation ----
	$(MAKE) -C $(PROGDIR) all CC=g++ CFLAGS="-O3 -Wall -Wextra -Wundef -Wshadow -Wcast-align -Werror"

armtest: clean
	@echo ---- test ARM compilation ----
	$(MAKE) -C $(PROGDIR) bin CC=arm-linux-gnueabi-gcc MOREFLAGS="-Werror"

clangtest: clean
	@echo ---- test clang compilation ----
	$(MAKE) -C $(PROGDIR) all CC=clang MOREFLAGS="-Werror -Wconversion -Wno-sign-conversion"

staticAnalyze: clean
	@echo ---- static analyzer - scan-build ----
	scan-build --status-bugs -v $(MAKE) -C $(PROGDIR) all CFLAGS=-g   # does not work well; too many false positives

sanitize: clean
	@echo ---- check undefined behavior - sanitize ----
	$(MAKE) test   -C $(PROGDIR) CC=clang MOREFLAGS="-g -fsanitize=undefined" FSETEST="-i5000" FSEU16TEST=-i2000
	$(MAKE) test32 -C $(PROGDIR) CC=clang MOREFLAGS="-g -fsanitize=undefined" FSETEST="-i5000" FSEU16TEST=-i2000


