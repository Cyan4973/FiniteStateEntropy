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


//**************************************
//* Includes
//**************************************
#include <stddef.h>    // size_t, ptrdiff_t


//**************************************
// FSE simple functions
//**************************************
int FSE_compress   (void* dest,
                    const unsigned char* source, int sourceSize);
int FSE_decompress (unsigned char* dest, int originalSize,
                    const void* compressed);
/*
FSE_compress():
    Compress table of unsigned char 'source', of size 'sourceSize', into destination buffer 'dest'.
    'dest' buffer must be already allocated, and sized to handle worst case situations.
    Use FSE_compressBound() to determine this size.
    return : size of compressed data
FSE_decompress():
    Decompress compressed data from buffer 'compressed',
    into destination table of unsigned char 'dest', of size 'originalSize'.
    Destination table must be already allocated, and large enough to accomodate 'originalSize' char.
    The function will determine how many bytes are read from buffer 'compressed'.
    return : size of compressed data
*/


/* same as previously, but data is presented as a table of unsigned short (2 bytes per symbol).
   All symbol values within input table must be < nbSymbols.
   Maximum allowed 'nbSymbols' value is controlled by constant FSE_MAX_NB_SYMBOLS inside fse.c */
int FSE_compressU16  (void* dest,
                      const unsigned short* source, int sourceSize, int nbSymbols, int tableLog);
int FSE_decompressU16(unsigned short* dest, int originalSize,
                      const void* compressed);


#define FSE_MAX_HEADERSIZE 512
#define FSE_COMPRESSBOUND(size) (size + FSE_MAX_HEADERSIZE)
static inline int FSE_compressBound(int size) { return FSE_COMPRESSBOUND(size); }
/*
FSE_compressBound():
    Gives the maximum (worst case) size that can be reached by function FSE_compress.
    Used to know how much memory to allocate for destination buffer.
*/


/******************************************
   FSE advanced functions
******************************************/
/*
FSE_compress2():
    Same as FSE_compress(), but allows the selection of 'nbSymbols' and 'tableLog'
    Both parameters can be defined as '0' to mean : use default value
    The function will then assume that any unsigned char within 'source' has value < nbSymbols.
    note : If this condition is not respected, compressed data will be corrupted !
    return : size of compressed data
*/
int FSE_compress2 (void* dest, const unsigned char* source, int sourceSize, int nbSymbols, int tableLog);


/******************************************
   FSE detailed API
******************************************/
/*
int FSE_compress(char* dest, const char* source, int inputSize) does the following:
1. count symbol occurence from table source[] into table count[]
2. normalize counters so that sum(counting[]) == Power_of_2 (== 2^tableLog)
3. saves normalized counters to memory buffer using writeHeader()
4. builds encoding tables from the normalized counters
5. encodes the data stream using encoding tables

int FSE_decompress(char* dest, int originalSize, const char* compressed) performs:
1. reads normalized counters with readHeader()
2. builds decoding tables from the normalized counters
3. decodes the data stream using these decoding tables

The following API allows to target specific sub-functions.
*/

/* *** COMPRESSION *** */

int FSE_count(unsigned int* count, const unsigned char* source, int sourceSize, int maxNbSymbols);

int FSE_normalizeCount(unsigned int* normalizedCounter, int maxTableLog, unsigned int* count, int total, int nbSymbols);

static inline int FSE_headerBound(int nbSymbols, int tableLog) { (void)tableLog; return nbSymbols ? (nbSymbols*2)+1 : 512; }
int FSE_writeHeader(void* header, const unsigned int* normalizedCounter, int nbSymbols, int tableLog);

int FSE_sizeof_CTable(int nbSymbols, int tableLog);
int FSE_buildCTable(void* CTable, const unsigned int* normalizedCounter, int nbSymbols, int tableLog);

int FSE_compress_usingCTable (void* dest, const unsigned char* source, int sourceSize, const void* CTable);

/*
The first step is to count all symbols. FSE_count() provides one quick way to do this job.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have 'maxNbSymbols' cells.
'source' is assumed to be a table of char of size 'sourceSize' if 'maxNbSymbols' <= 256.
All values within 'source' MUST be < maxNbSymbols.
FSE_count() will return the highest symbol value detected into 'source' (necessarily <= 'maxNbSymbols', can be 0 if only 0 is present).
If there is an error, the function will return -1.

The next step is to normalize the frequencies, so that Sum_of_Frequencies == 2^tableLog.
You can use 'tableLog'==0 to mean "default value".
The result will be saved into a structure, called 'normalizedCounter', which is a table of unsigned int.
'normalizedCounter' must be already allocated, and have 'nbSymbols' cells.
FSE_normalizeCount() will ensure that sum of 'nbSymbols' frequencies is == 2 ^'tableLog', it also guarantees a minimum of 1 to any Symbol which frequency is >= 1.
FSE_normalizeCount() can work "in place" to preserve memory, using 'count' as both source and destination area.
The return value is the corrected tableLog (<=maxTableLog). It is necessary to retrieve it for next steps.
A result of '0' means that there is only a single symbol present.
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
Similar to FSE_count(), the convention is that 'source' is assumed to be a table of char of size 'sourceSize'
The function returns the size of compressed data (without header), or -1 if failed.
*/


/* *** DECOMPRESSION *** */

int FSE_readHeader (unsigned int* const normalizedCounter, int* nbSymbols, int* tableLog, const void* header);

int FSE_sizeof_DTable(int tableLog);
int FSE_buildDTable(void* DTable, const unsigned int* const normalizedCounter, int nbSymbols, int tableLog);

int FSE_decompress_usingDTable(unsigned char* dest, const int originalSize, const void* compressed, const void* DTable, const int tableLog);

/*
The first step is to get the normalized frequency of symbols.
This can be performed by reading a header with FSE_readHeader().
'normalizedCounter' must be already allocated, and have at least 'nbSymbols' cells.
In practice, that means it's necessary to know 'nbSymbols' beforehand,
or size it to handle worst case situations (typically 256).
FSE_readHeader will provide 'tableLog' and 'nbSymbols' stored into the header.
The result of FSE_readHeader() is the number of bytes read from 'header'.
The following values have special meaning :
return 2 : there is only a single symbol value. The value is provided into the second byte.
return 1 : data is uncompressed
If there is an error, the function will return -1.

The next step is to create the decompression tables 'DTable' from 'normalizedCounter'.
This is performed by the function FSE_buildDTable().
The space required by 'DTable' must be already allocated. Its size is provided by FSE_sizeof_DTable().

'DTable' can then be used to decompress 'compressed', with FSE_decompress_usingDTable().
FSE_decompress_usingDTable() will regenerate exactly 'originalSize' symbols, as a table of unsigned char.
The function returns the size of compressed data (without header), or -1 if failed.
*/


/******************************************
   FSE streaming API
******************************************/
void* FSE_initCompressionStream(void** op, ptrdiff_t* state, const void** symbolTT, const void** stateTable, const void* CTable);
void  FSE_encodeByte(ptrdiff_t* state, size_t* bitStream, int* bitpos, unsigned char symbol, const void* symbolTT, const void* stateTable);
static void FSE_addBits(size_t* bitStream, int* bitpos, int nbBits, size_t bitField);
static void FSE_flushBits(size_t* bitStream, void** op, int* bitpos);
int   FSE_closeCompressionStream(size_t bitStream, int bitPos, ptrdiff_t state, void* op, void* compressionStreamDescriptor, const void* CTable);

/*
We are now inside the core function FSE_compress_usingCTable().
The primary purpose of exposing its inner components is to allow the mixing of FSE coding
with other source bit data, including multiple FSE encoders in parallel.

A key property to keep in mind is that encoding and decoding are done **in reverse direction**.
So the first symbol you will encode is the last you will decode.
The same logic applies to any other bitstream value you want to insert into the bitstream.

You will need a few variables to track your bitStream. They are :

void* op;           // Your output buffer (must be already allocated)
void* compressionStreamDescriptor;   // Required to init and close the bitStream
void* CTable;       // Provided by FSE_buildCTable()
ptrdiff_t state;    // Store fractional bits
size_t bitStream=0; // Cache bitStream into register before writing it
int bitpos=0;       // Nb of bits already used within bitStream
void* symbolTT;     // Encoding Table n°1. Provided by init. Required by encodeByte.
void* stateTable;   // Encoding Table n°2. Provided by init. Required by encodeByte.


The first thing to do is to init the bitStream.
    void* compressionStreamDescriptor = FSE_initCompressionStream(&op, &state, &symbolTT, &stateTable, CTable);

You can then encode your input data, byte after byte.
You are free to choose the direction in which you encode, as long as you remember decoding will be done in reverse direction.
    FSE_encodeByte(&state, &bitStream, &bitpos, (unsigned char*)symbol, symbolTT, stateTable);

At any time, you can add any other bit sequence.
Note : maximum allowed nbBits is 25
    FSE_addBits(&bitStream, &bitpos, nbBits, bitField);

Writing data to memory is performed by the flush method.
This way, you can store several bitFields into bitStream, before calling flush a single time.
BitStream size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
The nb of bits already written into bitStream is stored into bitPos.
For information, FSE_encodeByte() never writes more than 'tableLog' bits at a time.
    FSE_flushBits(&bitStream, &op, &bitpos);

When you are done with compression, you must close the bitStream.
The function returns the size in bytes of the compressed stream.
If there is an error, it returns -1.
    int size = FSE_closeCompressionStream(bitStream, bitPos, state, op, compressionStreamDescriptor, CTable);
*/


const void* FSE_initDecompressionStream(const void** input, int* bitsConsumed, unsigned int* state, unsigned int* bitStream, const int tableLog);
unsigned char FSE_decodeSymbol(unsigned int* state, unsigned int bitStream, int* bitsConsumed, const void* DTable);
unsigned int FSE_readBits(int* bitsConsumed, unsigned int bitStream, int nbBits);
void FSE_updateBitStream(unsigned int* bitStream, int* bitsConsumed, const void** input);
int FSE_closeDecompressionStream(const void* decompressionStreamDescriptor, const void* input);

/*
Now is the turn to decompose FSE_decompress_usingDTable().
You will decode FSE_encoded symbols from the bitStream,
but also any other bitFields you put in, **in reverse order**.
So, typically, if you encoded from end to beginning, you will now decode from beginning to end.

You will need a few variables to track your bitStream. They are :

const void* input;        // Your input buffer (where compressed data is)
const void* decompressionStreamDescriptor;   // Required to init and close the bitStream
const void* DTable;       // Provided by FSE_buildDTable()
int   tableLog;           // Provided by FSE_readHeader()
unsigned int state;       // Store fractional bits.
unsigned int bitStream;   // Cache bitStream into register
int bitsConsumed;         // Nb of bits already used within bitStream

The first thing to do is to init the bitStream.
    decompressionStreamDescriptor = FSE_initDecompressionStream(&ip, &bitsConsumed, &state);

You can then decode your data, byte after byte.
Keep in mind data is decoded in reverse order.
    unsigned char symbol = FSE_decodeSymbol(&state, bitStream, &bitsConsumed, DTable);

You can also retrieve any bitfield you eventually stored into the bitStream (in reverse order)
Note : maximum allowed nbBits is 25
    unsigned int bitField = FSE_readBits(&bitsConsumed, bitStream, nbBits);

Reading data to memory is performed by the update method.
'bitConsumed' shall never > 32.
For information the maximum number of bits read by FSE_decodeSymbol() is 'tableLog'.
Don't hesitate to use this method frequently. The cost of this operation is very low.
    FSE_updateBitStream(&bitStream, &bitsConsumed, &ip);;

When you are done with compression, you can close the bitStream (optional).
The function returns the size in bytes of the compressed stream.
If there is an error, it returns -1.
    int size = FSE_closeDecompressionStream(decompressionStreamDescriptor, ip, bitsConsumed);
*/



/***********************************************************************
   *** inlined functions (for performance) ***
   GCC is not as good as visual to inline code from other *.c files
***********************************************************************/

static inline void FSE_flushBits(size_t* bitStream, void** p, int* bitpos)
{
    char** op = (char**)p;
    ** (size_t**) op = *bitStream;
    {
        size_t nbBytes = *bitpos >> 3;
        *bitpos &= 7;
        *op += nbBytes;
        *bitStream >>= nbBytes*8;
    }
}

static inline void FSE_addBits(size_t* bitStream, int* bitpos, int nbBits, size_t value)
{
    static const unsigned int mask[] = { 0, 1, 3, 7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF, 0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF,  0xFFFFFF, 0x1FFFFFF };   // up to 25 bits
    *bitStream |= (value & mask[nbBits]) << *bitpos;
    *bitpos += nbBits;
}


#if defined (__cplusplus)
}
#endif
