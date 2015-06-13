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

.PHONY: clean

default: test

all: test-all

test:
	@cd $(PROGDIR); $(MAKE) test

test-all:
	@cd $(PROGDIR); $(MAKE) clean; $(MAKE) test

clean:
	@cd $(PROGDIR); $(MAKE) clean

test-clang: clean
	@cd $(PROGDIR); $(MAKE) all CC=clang

