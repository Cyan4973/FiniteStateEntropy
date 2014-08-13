/*
  fileio.h - simple generic file i/o handler - header
  Copyright (C) Yann Collet 2013-2014
  GPL v2 License

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

  You can contact the author at :
  - Public forum : https://groups.google.com/forum/#!forum/lz4c
*/
#pragma once

#if defined (__cplusplus)
extern "C" {
#endif


//**************************************
// Special input/output constants
//**************************************
#define NULL_OUTPUT "null"
#define stdinmark "stdin"
#define stdoutmark "stdout"
//static char stdinmark[] = "stdin";
//static char stdoutmark[] = "stdout";
#ifdef _WIN32
#define nulmark "nul"
//static char nulmark[] = "nul";
#else
#define nulmark "/dev/null"
//static char nulmark[] = "/dev/null";
#endif


//**************************************
// Parameters
//**************************************
void FIO_overwriteMode(void);


//**************************************
// Stream/File functions
//**************************************
int compress_file (char* outfilename, char* infilename);
unsigned long long decompress_file (char* outfilename, char* infilename);


#if defined (__cplusplus)
}
#endif
