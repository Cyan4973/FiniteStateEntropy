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


/******************************************
   Compiler Options
******************************************/
#if defined(_MSC_VER) && !defined(__cplusplus)   // Visual Studio
#  define inline __inline           // Visual C is not C99, but supports some kind of inline
#endif


/******************************************
   Includes
******************************************/
#include <stddef.h>    // size_t, ptrdiff_t


/******************************************
   FSE simple functions
******************************************/
int FSE_compress   (void* dest,
                    const unsigned char* source, unsigned sourceSize);
int FSE_decompress (unsigned char* dest, unsigned originalSize,
                    const void* compressed);
/*
FSE_compress():
    Compress table of unsigned char 'source', of size 'sourceSize', into destination buffer 'dest'.
    'dest' buffer must be already allocated, and sized to handle worst case situations.
    Worst case size evaluation is provided by FSE_compressBound().
    return : size of compressed data
             or -1 if there is an error.
FSE_decompress():
    Decompress compressed data from buffer 'compressed',
    into destination table of unsigned char 'dest', of size 'originalSize'.
    Destination table must be already allocated, and large enough to accommodate 'originalSize' char.
    The function will determine how many bytes are read from buffer 'compressed'.
    return : size of compressed data
             or -1 if there is an error.
*/


#define FSE_MAX_HEADERSIZE 512
#define FSE_COMPRESSBOUND(size) (size + (size>>7) + FSE_MAX_HEADERSIZE)   /* Macro can be useful for static allocation */
unsigned FSE_compressBound(unsigned size);
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
    Same as FSE_compress(), but allows the selection of 'maxSymbolValue' and 'tableLog'
    Both parameters can be defined as '0' to mean : use default value
    return : size of compressed data
             or -1 if there is an error
*/
int FSE_compress2 (void* dest, const unsigned char* source, unsigned sourceSize, unsigned maxSymbolValue, unsigned tableLog);


/*
FSE_decompress_safe():
    Same as FSE_decompress(), but ensures that the decoder never reads beyond compressed + maxCompressedSize.
    note : you don't have to provide the exact compressed size. If you provide more, it's fine too.
    This function is safe against malicious data.
    return : size of compressed data
             or -1 if there is an error
*/
int FSE_decompress_safe (unsigned char* dest, unsigned originalSize, const void* compressed, unsigned maxCompressedSize);


/******************************************
   FSE detailed API
******************************************/
/*
int FSE_compress(char* dest, const char* source, int inputSize) does the following:
1. count symbol occurrence from table source[] into table count[]
2. normalize counters so that sum(count[]) == Power_of_2 (2^tableLog)
3. save normalized counters to memory buffer using writeHeader()
4. build encoding tables from normalized counters
5. encode the data stream using encoding tables

int FSE_decompress(char* dest, int originalSize, const char* compressed) performs:
1. read normalized counters with readHeader()
2. build decoding tables from normalized counters
3. decode the data stream using these decoding tables

The following API allows to target specific sub-functions.
*/

/* *** COMPRESSION *** */

int FSE_count(unsigned* count, const unsigned char* source, unsigned sourceSize, unsigned* maxSymbolValuePtr);

unsigned FSE_optimalTableLog(unsigned tableLog, unsigned sourceSize, unsigned maxSymbolValue);
int FSE_normalizeCount(short* normalizedCounter, unsigned tableLog, const unsigned* count, unsigned total, unsigned maxSymbolValue);

unsigned FSE_headerBound(unsigned maxSymbolValue, unsigned tableLog);
int FSE_writeHeader (void* headerBuffer, unsigned headerBufferSize, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

int FSE_sizeof_CTable(unsigned maxSymbolValue, unsigned tableLog);
int FSE_buildCTable(void* CTable, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

int FSE_compress_usingCTable (void* dest, const unsigned char* source, unsigned sourceSize, const void* CTable);

/*
The first step is to count all symbols. FSE_count() provides one quick way to do this job.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have 'maxNbSymbols' cells.
'source' is a table of char of size 'sourceSize'. All values within 'source' MUST be < *maxNbSymbolsPtr
*maxNbSymbolsPtr will be updated, with its real value (necessarily <= original value)
FSE_count() will return the number of occurrence of the most frequent symbol.
If there is an error, the function will return -1.

The next step is to normalize the frequencies.
FSE_normalizeCount() will ensure that sum of frequencies is == 2 ^'tableLog'.
It also guarantees a minimum of 1 to any Symbol which frequency is >= 1.
You can use input 'tableLog'==0 to mean "use default tableLog value".
If you are unsure of which tableLog value to use, you can optionally call FSE_optimalTableLog(),
which will provide the optimal valid tableLog given sourceSize, maxSymbolValue, and a user-defined maximum (0 means "default").

The result of FSE_normalizeCount() will be saved into a table,
called 'normalizedCounter', which is a table of signed short.
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValue+1' cells.
The return value is tableLog if everything proceeded as expected.
It is 0 if there is a single symbol within distribution.
If there is an error (typically, invalid tableLog value), the function will return -1.

'normalizedCounter' can be saved in a compact manner to a memory area using FSE_writeHeader().
'header' buffer must be already allocated.
For guaranteed success, buffer size must be at least FSE_headerBound().
The result of the function is the number of bytes written into 'header'.
If there is an error, the function will return -1 (for example, buffer size too small).

'normalizedCounter' can then be used to create the compression tables 'CTable'.
The space required by 'CTable' must be already allocated. Its size is provided by FSE_sizeof_CTable().
You can then use FSE_buildCTable() to fill 'CTable'.
In both cases, if there is an error, the function will return -1.

'CTable' can then be used to compress 'source', with FSE_compress_usingCTable().
Similar to FSE_count(), the convention is that 'source' is assumed to be a table of char of size 'sourceSize'
The function returns the size of compressed data (without header), or -1 if failed.
*/


/* *** DECOMPRESSION *** */

int FSE_readHeader (short* const normalizedCounter, unsigned* maxSymbolValuePtr, unsigned* tableLogPtr, const void* header);

int FSE_sizeof_DTable(unsigned tableLog);
int FSE_buildDTable (void* DTable, const short* const normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

int FSE_decompress_usingDTable(unsigned char* dest, const unsigned originalSize, const void* compressed, const void* DTable, const unsigned tableLog, unsigned fastMode);

/*
The first step is to obtain the normalized frequencies of symbols.
This can be performed by reading a header with FSE_readHeader().
'normalizedCounter' must be already allocated, and have at least '*maxSymbolValuePtr+1' cells.
In practice, that means it's necessary to know 'maxSymbolValue' beforehand,
or size the table to handle worst case situations (typically 256).
FSE_readHeader will provide 'tableLog' and 'maxSymbolValue' stored into the header.
The result of FSE_readHeader() is the number of bytes read from 'header'.
The following values have special meaning :
return 2 : there is only a single symbol value. The value is provided into the second byte.
return 1 : data is uncompressed
If there is an error, the function will return -1.

The next step is to create the decompression tables 'DTable' from 'normalizedCounter'.
This is performed by the function FSE_buildDTable().
The space required by 'DTable' must be already allocated. Its size is provided by FSE_sizeof_DTable().
The function will return 1 if table is compatible with fastMode, 0 otherwise.
If there is an error, the function will return -1.

'DTable' can then be used to decompress 'compressed', with FSE_decompress_usingDTable().
FSE_decompress_usingDTable() will regenerate exactly 'originalSize' symbols, as a table of unsigned char.
Only use fastMode if it was authorized by result of FSE_buildDTable(), otherwise decompression will fail.
The function returns the size of compressed data (without header), or -1 if failed.
*/


/******************************************
   FSE streaming API
******************************************/
typedef struct
{
    size_t bitContainer;
    int bitPos;
} bitStream_forward_t;

void* FSE_initCompressionStream(void** op);
void FSE_initStateAndPtrs(ptrdiff_t* state, const void** CTablePtr1, const void** CTablePtr2, const void* CTable);
void FSE_encodeByte(ptrdiff_t* state, bitStream_forward_t* bitC, unsigned char symbol, const void* CTablePtr1, const void* CTablePtr2);
static void FSE_addBits(bitStream_forward_t* bitC, size_t value, int nbBits);
static void FSE_flushBits(void** outPtr, bitStream_forward_t* bitC);
int FSE_closeCompressionStream(void* outPtr, bitStream_forward_t* bitC, void* compressionStreamDescriptor, int optionalId);

/*
These function allow the creation of custom streams, mixing mutiple tables and bit sources.
They are used by FSE_compress_usingCTable().

A key property to keep in mind is that encoding and decoding are done **in reverse direction**.
So the first symbol you will encode is the last you will decode, like a lifo stack.
This logic applies to any bitstream value inserted into the bitstream.

You will need a few variables to track your bitStream. They are :

void* op;           // Your output buffer (must be already allocated)
void* compressionStreamDescriptor;   // Required to init and close the bitStream
void* CTable;       // Provided by FSE_buildCTable()
ptrdiff_t state;    // Encode fractional bits
bitStream_forward_t bitStream={0,0}; // Store bitStream into register before writing it
void* CTablePtr1;   // Encoding Table n°1. Provided by init. Required by encodeByte.
void* CTablePtr2;   // Encoding Table n°2. Provided by init. Required by encodeByte.


The first thing to do is to init the bitStream.
    void* compressionStreamDescriptor = FSE_initCompressionStream(&op);

And then init your state and Ptrs. Ptrs are required by the encoded function.
    FSE_initStateAndPtrs(&state, &CTablePtr1, &CTablePtr2, CTable);

You can then encode your input data, byte after byte.
You are free to choose the direction in which you encode, as long as you remember decoding will be done in reverse direction.
    FSE_encodeByte(&state, &bitStream, symbol, CTablePtr1, CTablePtr2);

At any time, you can add any other bit sequence.
Note : maximum allowed nbBits is 25, to be compatible with 32-bits decoders
    FSE_addBits(&bitStream, bitField, nbBits);

Writing data to memory is performed by the flush method.
It's possible to store several bitFields into bitStream before calling flush a single time.
BitStream size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
The nb of bits already written into bitStream is stored into bitPos.
For information, FSE_encodeByte() never writes more than 'tableLog' bits at a time.
    FSE_flushBits(&op, &bitStream);

Your last FSE encoding operation shall be to flush your last state value.
    FSE_addBits(&bitStream, state, tableLog);
    FSE_flushBits(&op, &bitStream);

When you are done with compression, you must close the bitStream.
It's possible to embed an optionalId into the header, for later information, value must be between 1 and 4.
The function returns the size in bytes of the compressed stream.
If there is an error, it returns -1.
    int size = FSE_closeCompressionStream(op, &bitStream, compressionStreamDescriptor, optionalId);
*/

typedef struct
{
    unsigned int bitContainer;
    int bitsConsumed;
} bitStream_backward_t;

const void* FSE_initDecompressionStream (const void** p, bitStream_backward_t* bitC, unsigned* optionalId);
const void* FSE_initDecompressionStream_safe (const void** p, bitStream_backward_t* bitC, unsigned* optionalId, unsigned maxCompressedSize);
unsigned char FSE_decodeSymbol(unsigned int* state, bitStream_backward_t* bitC, const void* DTable, unsigned fast);
unsigned int FSE_readBits(bitStream_backward_t* bitC, unsigned nbBits);
void FSE_updateBitStream(bitStream_backward_t* bitC, const void** ip);
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
unsigned int state;       // Encoded fractional bits
bitStream_backward_t bitStream;   // Store bits read from input

The first thing to do is to init the bitStream.
    decompressionStreamDescriptor = FSE_initDecompressionStream(&ip, &bitStream, &optionalId);

You should then retrieve your initial state value :
    state = FSE_readBits(&bitC, tableLog);
    FSE_updateBitStream(&bitStream, &ip);

You can then decode your data, byte after byte.
Keep in mind data is decoded in reverse order, like a lifo container.
    unsigned char symbol = FSE_decodeSymbol(&state, bitStream, &bitsConsumed, DTable);

You can retrieve any other bitfield you eventually stored into the bitStream (in reverse order)
Note : maximum allowed nbBits is 25
    unsigned int bitField = FSE_readBits(&bitStream, nbBits);

Reading data to memory is performed by the update method.
'bitConsumed' shall never > 32.
For information the maximum number of bits read by FSE_decodeSymbol() is 'tableLog'.
Don't hesitate to use this method. The cost of this operation is very low.
    FSE_updateBitStream(&bitStream, &ip);

When you are done with decompression, you can close the bitStream (optional).
The function returns the size in bytes of the compressed stream.
If there is an error, it returns -1.
    int size = FSE_closeDecompressionStream(decompressionStreamDescriptor, ip, bitsConsumed);
*/


/***********************************************************************
   *** inlined functions (for performance) ***
   GCC is not as good as visual to inline code from other *.c files
***********************************************************************/

static inline void FSE_addBits(bitStream_forward_t* bitC, size_t value, int nbBits)
{
    static const unsigned int mask[] = { 0, 1, 3, 7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF, 0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF,  0xFFFFFF, 0x1FFFFFF };   // up to 25 bits
    bitC->bitContainer |= (value & mask[nbBits]) << bitC->bitPos;
    bitC->bitPos += nbBits;
}

static inline void FSE_flushBits(void** outPtr, bitStream_forward_t* bitC)
{
    ** (size_t**) outPtr = bitC->bitContainer;
    {
        size_t nbBytes = bitC->bitPos >> 3;
        bitC->bitPos &= 7;
        *(char**)outPtr += nbBytes;
        bitC->bitContainer >>= nbBytes*8;
    }
}


#if defined (__cplusplus)
}
#endif
