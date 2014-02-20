/*
fse2t.h
FSE coder using Escape Strategy
Copyright (C) Yann Collet 2012-2014
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
*/



//**************************************
// Simple Functions
//**************************************
int FSE2T_compress(void* dst, const unsigned char* src, int srcSize);
int FSE2T_decompress (unsigned char* dest, int originalSize, const void* compressed);


//**************************************
// Advanced Functions
//**************************************
int FSE2T_compress2(void* dst, const unsigned char* src, int srcSize, int tableLog);

int FSE2T_compress_usingCTable  (void* dest, const unsigned char* source, int sourceSize, const void* CTable, const unsigned char* translate, const void* escapeCTable, unsigned char escapeSymbol);
int FSE2T_decompress_usingDTable(unsigned char* dest, const int originalSize, const void* compressed, const void* DTable, const int tableLog, const void* escapeDTable, unsigned char escapeSymbol);


