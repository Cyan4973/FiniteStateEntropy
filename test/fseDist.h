/*
fseDist.h
Universal length FSE coder
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



int FSED_compressU16 (void* dest, const unsigned short* source, unsigned sourceSize, unsigned memLog);
int FSED_decompressU16 (unsigned short* dest, unsigned originalSize, const void* compressed);


int FSED_compressU16Log2 (void* dest, const unsigned short* source, int sourceSize, int memLog);
int FSED_decompressU16Log2 (unsigned short* dest, int originalSize, const void* compressed);


int FSED_compressU32 (void* dest, const unsigned int* source, int sourceSize, int memLog);
int FSED_decompressU32 (unsigned int* dest, int originalSize, const void* compressed);
