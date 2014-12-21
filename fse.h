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
   Includes
******************************************/
#include <stddef.h>    // size_t, ptrdiff_t


/******************************************
   FSE simple functions
******************************************/
size_t FSE_compress(void* dst, size_t dstSize,
              const void* src, size_t srcSize);
int FSE_decompress (unsigned char* dst, unsigned originalSize,
                    const void* compressed);
/*
FSE_compress():
    Compress content of buffer 'src', of size 'srcSize', into destination buffer 'dst'.
    'dst' buffer must be already allocated, and sized to handle worst case situations.
    Worst case size evaluation is provided by FSE_compressBound().
    return : size of compressed data
             or an error value, which can be tested using FSE_isError()

FSE_decompress():
    Decompress compressed data from buffer 'compressed',
    into destination table of unsigned char 'dest', of size 'originalSize'.
    Destination table must be already allocated, and large enough to accommodate 'originalSize' char.
    The function will determine how many bytes are read from buffer 'compressed'.
    return : size of compressed data
             or -1 if there is an error.
*/


/******************************************
   Tool functions
******************************************/
size_t FSE_compressBound(size_t size);       /* maximum compressed size */

/* Error Management */
unsigned    FSE_isError(size_t code);        /* tells if a return value is an error code */
const char* FSE_getErrorName(size_t code);   /* provides error code string (useful for debugging) */



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
size_t FSE_compress2 (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog);


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

size_t FSE_count(unsigned* count, const unsigned char* src, size_t srcSize, unsigned* maxSymbolValuePtr);

unsigned FSE_optimalTableLog(unsigned tableLog, size_t srcSize, unsigned maxSymbolValue);
size_t FSE_normalizeCount(short* normalizedCounter, unsigned tableLog, const unsigned* count, size_t total, unsigned maxSymbolValue);

size_t FSE_headerBound(unsigned maxSymbolValue, unsigned tableLog);
size_t FSE_writeHeader (void* headerBuffer, size_t headerBufferSize, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

size_t FSE_sizeof_CTable(unsigned maxSymbolValue, unsigned tableLog);
size_t FSE_buildCTable(void* CTable, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

size_t FSE_compress_usingCTable (void* dst, size_t dstSize, const void* src, size_t srcSize, const void* CTable);

/*
The first step is to count all symbols. FSE_count() provides one quick way to do this job.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have '*maxSymbolValuePtr+1' cells.
'source' is a table of char of size 'sourceSize'. All values within 'src' MUST be <= *maxSymbolValuePtr
*maxSymbolValuePtr will be updated, with its real value (necessarily <= original value)
FSE_count() will return the number of occurrence of the most frequent symbol.
If there is an error, the function will return an ErrorCode (which can be tested using FSE_isError()).

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
If there is an error(typically, invalid tableLog value), the function will return an ErrorCode (which can be tested using FSE_isError()).

'normalizedCounter' can be saved in a compact manner to a memory area using FSE_writeHeader().
'header' buffer must be already allocated.
For guaranteed success, buffer size must be at least FSE_headerBound().
The result of the function is the number of bytes written into 'header'.
If there is an error, the function will return an ErrorCode (which can be tested using FSE_isError()) (for example, buffer size too small).

'normalizedCounter' can then be used to create the compression tables 'CTable'.
The space required by 'CTable' must be already allocated. Its size is provided by FSE_sizeof_CTable().
'CTable' must be aligned of 4 bytes boundaries.
You can then use FSE_buildCTable() to fill 'CTable'.
In both cases, if there is an error, the function will return an ErrorCode (which can be tested using FSE_isError()).

'CTable' can then be used to compress 'source', with FSE_compress_usingCTable().
Similar to FSE_count(), the convention is that 'source' is assumed to be a table of char of size 'sourceSize'
The function returns the size of compressed data (without header), or -1 if failed.
*/


/* *** DECOMPRESSION *** */

int FSE_readHeader (short* normalizedCounter, unsigned* maxSymbolValuePtr, unsigned* tableLogPtr, const void* header);

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
    int    bitPos;
    char*  startPtr;
    char*  ptr;
} FSE_CStream_t;

typedef struct
{
    ptrdiff_t   value;
    const void* stateTable;
    const void* symbolTT;
    unsigned    stateLog;
} FSE_CState_t;

void   FSE_initCStream(FSE_CStream_t* bitC, void* dstBuffer);
void*  FSE_getCStreamStart(const FSE_CStream_t* bitC);
void   FSE_reserveCStreamSize(FSE_CStream_t* bitC, size_t size);

void   FSE_initCState(FSE_CState_t* CStatePtr, const void* CTable);
void   FSE_encodeByte(FSE_CStream_t* bitC, FSE_CState_t* CStatePtr, unsigned char symbol);
void   FSE_addBits(FSE_CStream_t* bitC, size_t value, unsigned nbBits);
void   FSE_flushBits(FSE_CStream_t* bitC);

void   FSE_flushCState(FSE_CStream_t* bitC, const FSE_CState_t* CStatePtr);
size_t FSE_closeCStream(FSE_CStream_t* bitC, unsigned optionalId);

/*
These functions are inner components of FSE_compress_usingCTable().
They allow creation of custom streams, mixing multiple tables and bit sources.

A key property to keep in mind is that encoding and decoding are done **in reverse direction**.
So the first symbol you will encode is the last you will decode, like a lifo stack.

You will need a few variables to track your CStream. They are :

void* CTable;           // Provided by FSE_buildCTable()
FSE_CStream_t bitC;     // bitStream tracking structure
FSE_CState_t state;     // State tracking structure


The first thing to do is to init the bitStream, and the state.
    FSE_initCStream(&bitC, dstBuffer);
    FSE_initState(&state, CTable);


Right afterwards, you can optionnally insert some customized block of data,
for example a header, using the following functions :
    pos = FSE_getCStreamStart(&bitC);
    FSE_reserveCStreamSize(&bitC, customSize);
    memcpy(pos, blockOfData, customSize);
You can reserve multiple blocks this way. 
For example, if you did now :
    pos2 = FSE_getCStreamStart(&bitC);
you'll notice that : pos2 == pos + customSize.
Remember you'll have to notify the decoder stream
about the presence *and size* of such data blocks
at the beginning of decompression.


You can then encode your input data, byte after byte.
Remember decoding will be done in reverse direction.
    FSE_encodeByte(&bitStream, &state, symbol);

At any time, you can add any bit sequence.
Note : maximum allowed nbBits is 25, for compatibility with 32-bits decoders
    FSE_addBits(&bitStream, bitField, nbBits);

The above methods don't commit data to memory, they just store it into local register.
Local register size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
Writing data to memory is performed by the flushBits method.
(PS : FSE_encodeByte() never writes more than 'tableLog' bits at a time)
    FSE_flushBits(&bitStream);

Your last FSE encoding operation shall be to flush your last state value(s).
    FSE_flushState(&bitStream, &state);

When you are done with compression, you must close the bitStream.
It's possible to embed an optionalId into the header. It will be decoded by decompressor.
The function returns the size in bytes of the compressed stream.
If there is an error, it returns an errorCode (which can be tested using FSE_isError()).
    size_t size = FSE_closeCStream(&bitStream, optionalId);
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


/*** new   ***/

typedef struct
{
    unsigned bitContainer;
    unsigned bitsConsumed;
    const char* ptr;
    const char* endPtr;
} bitReadB_t;

typedef struct
{
    unsigned    state;
    const void* table;
} FSE_DState_t;

size_t FSE_DStream_getSize(void* srcBuffer, size_t srcSize, unsigned* optInfo);
size_t FSE_DStream_init(bitReadB_t* bitD, unsigned* optInfo, const void* srcBuffer, size_t srcSize);

void*  FSE_DStream_getStart(const bitReadB_t* bitD);
size_t FSE_DStream_skip(bitReadB_t* bitD, size_t size);

void   FSE_initDState(FSE_DState_t* DStatePtr, bitReadB_t* bitD, const void* DTable, unsigned tableLog);

unsigned char FSE_decodeSymbol2(FSE_DState_t* DStatePtr, bitReadB_t* bitD);
unsigned int  FSE_readBits2(bitReadB_t* bitD, unsigned nbBits);
unsigned int  FSE_readBitsFast2(bitReadB_t* bitD, unsigned nbBits);
void          FSE_DStream_reload(bitReadB_t* bitD);

size_t FSE_endOfBitStream(const FSE_DState_t* DStatePtr, const bitReadB_t* bitD);


/*
Now is the turn to decompose FSE_decompress_usingDTable().
You will decode FSE_encoded symbols from the bitStream,
but also any other bitFields you put in, **in reverse order**.

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


#if defined (__cplusplus)
}
#endif
