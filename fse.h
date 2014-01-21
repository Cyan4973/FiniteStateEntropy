/* ******************************************************************
   FSE : Finite State Entropy coder
   header file
   Copyright (C) 2013-2014, Yann Collet.
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */
#pragma once

#if defined (__cplusplus)
extern "C" {
#endif


//**************************************
// Compiler Options
//**************************************
#if defined(_MSC_VER) && !defined(__cplusplus)   // Visual Studio
#  define inline __inline           // Visual C is not C99, but supports some kind of inline
#endif


//****************************
// FSE simple functions
//****************************
int FSE_compress   (void* dest,
                    const void* source, int sourceSize);
int FSE_decompress (void* dest, int originalSize,
                    const void* compressed);
/*
FSE_compress():
    Compress memory buffer 'source', of size 'sourceSize', into destination buffer 'dest'.
    'dest' buffer must be already allocated, and sized to handle worst case situations.
    Use FSE_compressBound() to determine this size.
    return : size of compressed data
FSE_decompress():
    Decompress compressed data from buffer 'compressed',
    into destination buffer 'dest', of size 'originalSize'.
    Destination buffer must be already allocated, and large enough to accomodate 'originalSize' bytes.
    The function will determine how many bytes are read from buffer 'compressed'.
    return : size of compressed data
*/


#define FSE_MAX_HEADERSIZE 512
#define FSE_COMPRESSBOUND(size) (size + FSE_MAX_HEADERSIZE)
static inline int FSE_compressBound(int size) { return FSE_COMPRESSBOUND(size); }
/*
FSE_compressBound():
    Gives the maximum (worst case) size that can be reached by function FSE_compress.
    Used to know how much memory to allocate for destination buffer.
*/

//****************************
// FSE advanced functions
//****************************
int FSE_compress_Nsymbols (void* dest, const void* source, int sourceSize, int nbSymbols);
/*
FSE_compress_Nsymbols():
    Minor variant of FSE_compress(), where the number of symbols can be specified.
    The function will then assume that any byte within 'source' has value < nbSymbols.
    note : If this condition is not respected, compressed data will be corrupted !
    return : size of compressed data
*/


//******************************************
// FSE detailed API (experimental)
//******************************************
/*
int FSE_compress(char* dest, const char* source, int inputSize) does the following:
1. calculates symbol distribution of the source[] into internal table counting[c]
2. normalizes counters so that sum(counting[c]) == Power_of_2 (== 2^memLog)
3. saves normalized counters to the file using writeHeader()
4. builds encoding tables from the normalized counters
5. encodes the data stream using only these encoding tables

int FSE_decompress(char* dest, int originalSize, const char* compressed) performs:
1. reads normalized counters with readHeader()
2. builds decoding tables from the normalized counters
3. decodes the data stream using only these decoding tables

The following API allows to target specific sub-functions.
*/

/* *** COMPRESSION *** */

int FSE_count(unsigned int* count, const void* source, int sourceSize, int maxNbSymbols);

int FSE_normalizeCount(unsigned int* normalizedCounter, int tableLog, unsigned int* count, int total, int nbSymbols);

static inline int FSE_headerBound(int nbSymbols, int memLog) { (void)memLog; return nbSymbols ? (nbSymbols*2)+1 : 512; }
int FSE_writeHeader(void* header, const unsigned int* normalizedCounter, int nbSymbols, int tableLog);

int FSE_sizeof_CTable(int nbSymbols, int tableLog);
int FSE_buildCTable(void* CTable, const unsigned int* normalizedCounter, int nbSymbols, int tableLog);

int FSE_compress_usingCTable (void* dest, const void* source, int sourceSize, const void* CTable);

/*
The first step is to count all symbols. FSE_count() provides one quick way to do this job.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have 'maxNbSymbols' cells.
'source' is assumed to be a table of char of size 'sourceSize' if 'maxNbSymbols' <= 256.
All values within 'source' MUST be < maxNbSymbols.
FSE_count() will return the highest symbol value detected into 'source' (necessarily <= 'maxNbSymbols', can be 0 if only 0 is present).
If there is an error, the function will return -1.

The next step is to normalize the frequencies, so that Sum_of_Frequencies == 2^tableLog.
You can use 'tableLog'==0 to mean "default value".
The result will be saved into a structure, called 'normalizedCounter', which is basically a table of unsigned int.
'normalizedCounter' must be already allocated, and have 'nbSymbols' cells.
FSE_normalizeCount() will ensure that sum of 'nbSymbols' frequencies is == 2 ^'memlog', it also guarantees a minimum of 1 to any Symbol which frequency is >= 1.
FSE_normalizeCount() can work "in place" to preserve memory, using 'count' as both source and destination area.
A result of '0' means that the normalization was completed successfully.
A result > 0 means that there is only a single symbol present, and its value is (result-1).
If there is an error, the function will return -1.

'normalizedCounter' can be saved in a compact manner to a memory area using FSE_writeHeader().
The target memory area must be pointed by 'header'.
'header' buffer must be already allocated. Its size must be at least FSE_headerBound().
The result of the function is the number of bytes written into 'header'.
If there is an error, the function will return -1.

'normalizedCounter' can then be used to create the compression tables 'CTable'.
The space required by 'CTable' must be already allocated.
Its size is provided by FSE_sizeof_CTable().
In both cases, if there is an error, the function will return -1.

'CTable' can then be used to compress 'source', with FSE_compress_usingCTable().
Similar to FSE_count(), the convention is that 'source' is assumed to be a table of char of size 'sourceSize' if 'nbSymbols' <= 256.
The function returns the size of compressed data (without header), or -1 if failed.
*/


/* *** DECOMPRESSION *** */

int FSE_readHeader (unsigned int* const normalizedCounter, int* nbSymbols, int* tableLog, const void* header);

int FSE_sizeof_DTable(int tableLog);
int FSE_buildDTable(void* DTable, const unsigned int* const normalizedCounter, int nbSymbols, int tableLog);

int FSE_decompress_usingDTable(void* dest, const int originalSize, const void* compressed, const void* DTable, const int tableLog);

/*
The first step is to get the normalized frequency of symbols.
This can be performed in many ways, reading a header with FSE_readHeader() being one them.
'normalizedCounter' must be already allocated, and have at least 'nbSymbols' cells.
In practice, that means it's necessary to know 'nbSymbols' beforehand,
or size it to handle worst case situations (typically 'FSE_NBSYMBOLS_DEFAULT').
FSE_readHeader will provide 'tableLog" and 'nbSymbols' stored into the header.
The result of FSE_readHeader() is the number of bytes read from 'header'.
The following values have special meaning :
return 2 : there is only a single symbol value. The value is provided into the second byte.
return 1 : data is uncompressed
If there is an error, the function will return -1.

The next step is to create the decompression tables 'DTable' from 'normalizedCounter'.
This is performed by the function FSE_buildDTable().
The space required by 'DTable' must be already allocated. Its size is provided by FSE_sizeof_DTable().

'DTable' can then be used to decompress 'compressed', with FSE_decompress_usingDTable().
FSE_decompress_usingDTable() will regenerate exactly 'originalSize' symbols, as a table of char if 'nbSymbols' <= 256.
The function returns the size of compressed data (without header), or -1 if failed.
*/


#if defined (__cplusplus)
}
#endif
