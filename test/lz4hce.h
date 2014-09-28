/*
    LZ4 HC Extractor - Generates selected fields from LZ4 HC algorithm
    Header File
    Copyright (C) Yann Collet 2013-2014
    GPLv2 License

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
    Public forum : https://groups.google.com/forum/#!forum/lz4c
*/
#pragma once


#if defined (__cplusplus)
extern "C" {
#endif

typedef enum { et_runLength, et_runLengthU16, et_runLengthU32, et_runLengthLN, et_literals,
               et_offsetHigh, et_offsetU16, et_offsetU32, et_offset, et_runLength285,
               et_matchLengthLog2, et_matchLengthU16, et_matchLength, et_lastbits, et_final,
    } extractionType;

int LZ4_extractHC (const char* source, char* dest, int inputSize, extractionType eType);
/*
LZ4_extractHC :
    return : the number of bytes into dest buffer
             or 0 if compression fails.
    note : destination buffer must be already allocated.
*/



#if defined (__cplusplus)
}
#endif
