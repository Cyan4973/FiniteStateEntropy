/* ******************************************************************
   FSE : Finite State Entropy coder
   header file
   Copyright (C) 2013-2015, Yann Collet.

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
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
   - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */
#pragma once

#if defined (__cplusplus)
extern "C" {
#endif


/******************************************
*  Includes
******************************************/
#include <stddef.h>    /* size_t, ptrdiff_t */


/******************************************
*  FSE simple functions
******************************************/
size_t FSE_compress(void* dst, size_t maxDstSize,
              const void* src, size_t srcSize);
size_t FSE_decompress(void* dst,  size_t maxDstSize,
                const void* cSrc, size_t cSrcSize);
/*
FSE_compress():
    Compress content of buffer 'src', of size 'srcSize', into destination buffer 'dst'.
    'dst' buffer must be already allocated, and sized to handle worst case situations.
    Worst case size evaluation is provided by FSE_compressBound().
    return : size of compressed data
    Special values : if return == 0, srcData is not compressible => Nothing is stored within cSrc !!!
                     if return == 1, srcData is a single byte symbol * srcSize times. Use RLE compression.
                     if FSE_isError(return), it's an error code.

FSE_decompress():
    Decompress FSE data from buffer 'cSrc', of size 'cSrcSize',
    into already allocated destination buffer 'dst', of size 'maxDstSize'.
    return : size of regenerated data (<= maxDstSize)
             or an error code, which can be tested using FSE_isError()

    ** Important ** : FSE_decompress() doesn't decompress non-compressible nor RLE data !!!
    Why ? : making this distinction requires a header.
    FSE library doesn't manage headers, which are intentionally left to the user layer.
*/


/******************************************
*  Tool functions
******************************************/
size_t FSE_compressBound(size_t size);       /* maximum compressed size */

/* Error Management */
unsigned    FSE_isError(size_t code);        /* tells if a return value is an error code */
const char* FSE_getErrorName(size_t code);   /* provides error code string (useful for debugging) */


/******************************************
*  FSE advanced functions
******************************************/
/*
FSE_compress2():
    Same as FSE_compress(), but allows the selection of 'maxSymbolValue' and 'tableLog'
    Both parameters can be defined as '0' to mean : use default value
    return : size of compressed data
    Special values : if return == 0, srcData is not compressible => Nothing is stored within cSrc !!!
                     if return == 1, srcData is a single byte symbol * srcSize times. Use RLE compression.
                     if FSE_isError(return), it's an error code.
*/
size_t FSE_compress2 (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog);


/******************************************
*  FSE detailed API
******************************************/
/*
FSE_compress() does the following:
1. count symbol occurrence from source[] into table count[]
2. normalize counters so that sum(count[]) == Power_of_2 (2^tableLog)
3. save normalized counters to memory buffer using writeHeader()
4. build encoding table 'CTable' from normalized counters
5. encode the data stream using encoding table 'CTable'

FSE_decompress() does the following:
1. read normalized counters with readHeader()
2. build decoding table 'DTable' from normalized counters
3. decode the data stream using decoding table 'DTable'

The following API allows to trigger specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and provide normalized distribution using one's own method.
*/

/* *** COMPRESSION *** */

/*
FSE_count():
   Provides the precise count of each symbol within a table 'count'
   'count' is a table of unsigned int, of minimum size (maxSymbolValuePtr[0]+1).
   maxSymbolValuePtr[0] will be updated if detected smaller than initially expected
   return : the count of the most frequent symbol (which is not identified)
            if return == srcSize, there is only one symbol.
            if FSE_isError(return), it's an error code. */
size_t FSE_count(unsigned* count, unsigned* maxSymbolValuePtr, const unsigned char* src, size_t srcSize);

/*
FSE_optimalTableLog():
   dynamically downsize 'tableLog' when conditions are met.
   It saves CPU time, by using smaller tables, while preserving or even improving compression ratio.
   return : recommended tableLog (necessarily <= initial 'tableLog') */
unsigned FSE_optimalTableLog(unsigned tableLog, size_t srcSize, unsigned maxSymbolValue);

/*
FSE_normalizeCount():
   normalize counters so that sum(count[]) == Power_of_2 (2^tableLog)
   'normalizedCounter' is a table of short, of minimum size (maxSymbolValue+1).
   return : tableLog,
            or an errorCode, which can be tested using FSE_isError() */
size_t FSE_normalizeCount(short* normalizedCounter, unsigned tableLog, const unsigned* count, size_t srcSize, unsigned maxSymbolValue);

/*
FSE_headerBound():
   Provides the maximum possible size of an FSE header, given 'maxSymbolValue' and 'tableLog'
   Useful for allocation purpose */
size_t FSE_headerBound(unsigned maxSymbolValue, unsigned tableLog);

/*
FSE_writeHeader():
   Compactly save 'normalizedCounter' into buffer 'headerBuffer' of size 'headerBufferSize'.
   return : size of the compressed table
            or an errorCode, which can be tested using FSE_isError() */
size_t FSE_writeHeader (void* headerBuffer, size_t headerBufferSize, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);


/*
Constructor and Destructor of type CTable
Not that its size depends on parameters 'tableLog' and 'maxSymbolValue' */
typedef unsigned* CTable;   /* enforce alignment on 4-bytes */
CTable FSE_createCTable (unsigned tableLog, unsigned maxSymbolValue);
void   FSE_freeCTable (CTable ct);

/*
FSE_buildCTable():
   Builds CTable, which must be already allocated, using FSE_createCTable()
   return : 0
            or an errorCode, which can be tested using FSE_isError() */
size_t   FSE_buildCTable(CTable ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*
FSE_compress_usingCTable():
   Compress 'src' using 'CTable' into 'dst' which must be already allocated
   return : size of compressed data
            or an errorCode, which can be tested using FSE_isError() */
size_t FSE_compress_usingCTable (void* dst, size_t dstSize, const void* src, size_t srcSize, const CTable ct);

/*
Tutorial :
----------
The first step is to count all symbols. FSE_count() provides one quick way to do this job.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have 'maxSymbolValuePtr[0]+1' cells.
'src' is a table of bytes of size 'srcSize'. All values within 'src' MUST be <= maxSymbolValuePtr[0]
maxSymbolValuePtr[0] will be updated, with its real value (necessarily <= original value)
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

'normalizedCounter' can then be used to create the compression table 'CTable'.
The space required by 'CTable' must be already allocated. Its size is provided by FSE_sizeof_CTable().
'CTable' must be aligned of 4 bytes boundaries.
You can then use FSE_buildCTable() to fill 'CTable'.
In both cases, if there is an error, the function will return an ErrorCode (which can be tested using FSE_isError()).

'CTable' can then be used to compress 'src', with FSE_compress_usingCTable().
Similar to FSE_count(), the convention is that 'src' is assumed to be a table of char of size 'srcSize'
The function returns the size of compressed data (without header).
If there is an error, the function will return an ErrorCode (which can be tested using FSE_isError()).
*/


/* *** DECOMPRESSION *** */

/*
FSE_readHeader():
   Read compactly saved 'normalizedCounter' from buffer 'headerBuffer' of size 'hbSize'.
   return : size of the compressed table
            or an errorCode, which can be tested using FSE_isError()
            maxSymbolValuePtr[0] and tableLogPtr[0] will also be updated with their respective values */
size_t FSE_readHeader (short* normalizedCounter, unsigned* maxSymbolValuePtr, unsigned* tableLogPtr, const void* headerBuffer, size_t hbSize);

/*
Constructor and Destructor of type DTable
Not that its size depends on parameters 'tableLog'*/
typedef unsigned* DTable;
DTable FSE_createDTable(unsigned tableLog);
void   FSE_freeDTable(DTable dt);

/*
FSE_buildDTable():
   Builds DTable, which must be already allocated, using FSE_createDTable()
   return : 1 is DTable is compatible with fast mode, 0 otherwise,
            or an errorCode, which can be tested using FSE_isError() */
size_t FSE_buildDTable (DTable dt, const short* const normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*
FSE_decompress_usingDTable():
   Decompress compressed source 'cSrc' of size 'cSrcSize'
   using 'DTable' into 'dst' which must be already allocated.
   Use fastMode==1 only if authorized by result of FSE_buildDTable().
   return : size of regenerated data (necessarily <= maxDstSize)
            or an errorCode, which can be tested using FSE_isError() */
size_t FSE_decompress_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const DTable dt, size_t fastMode);

/*
Tutorial :
----------
(Note : these functions only decompress FSE-compressed blocks.
 If block is uncompressed, use memcpy() instead
 If block is a single repeated byte, use memset() instead )

The first step is to obtain the normalized frequencies of symbols.
This can be performed by reading a header with FSE_readHeader().
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValuePtr[0]+1' cells of short.
In practice, that means it's necessary to know 'maxSymbolValue' beforehand,
or size the table to handle worst case situations (typically 256).
FSE_readHeader will provide 'tableLog' and 'maxSymbolValue' stored into the header.
The result of FSE_readHeader() is the number of bytes read from 'header'.
If there is an error, the function will return an error code, which can be tested using FSE_isError().

The next step is to create the decompression tables 'DTable' from 'normalizedCounter'.
This is performed by the function FSE_buildDTable().
The space required by 'DTable' must be already allocated using FSE_createDTable().
The function will return 1 if DTable is compatible with fastMode, 0 otherwise.
If there is an error, the function will return an error code, which can be tested using FSE_isError().

'DTable' can then be used to decompress 'cSrc', with FSE_decompress_usingDTable().
Only trigger fastMode if it was authorized by the result of FSE_buildDTable(), otherwise decompression will fail.
cSrcSize must be correct, otherwise decompression will fail.
FSE_decompress_usingDTable() result will tell how many bytes were regenerated.
If there is an error, the function will return an error code, which can be tested using FSE_isError().
*/


/******************************************
*  FSE symbol compression API
******************************************/
/*
   This API consists of small unitary functions, which highly benefit from being inlined.
   Some compilers seem unable to properly inline functions from another *.c (GCC, clang)
   while others have no such issue (Visual).
   If you find yourself in the first situation, you might want to include "fse.c",
   to improve the likelyhood of inlining these functions, which is key to their performance.
*/


/* Note :
   It is tempting to make below types opaque, since users should never access their member.
   But it's also necessary to let user allocate such structures on stack,
   because it's significantly good for performance and memory management.
   A simple way is to let the struct definition here, in the .h.
   Alternately, it could be possible to publish a "generic struct" with good size and alignment (like xxhash and lz4).
   A downside is a need to constantly cast to proper internal type within the .c. */

typedef struct { size_t space[4]; } FSE_CStream_t;

typedef struct
{
    ptrdiff_t   value;
    const void* stateTable;
    const void* symbolTT;
    unsigned    stateLog;
} FSE_CState_t;


void   FSE_initCStream(FSE_CStream_t* bitC, void* dstBuffer);
void   FSE_initCState(FSE_CState_t* CStatePtr, const CTable ct);

void   FSE_encodeByte(FSE_CStream_t* bitC, FSE_CState_t* CStatePtr, unsigned char symbol);
void   FSE_addBits(FSE_CStream_t* bitC, size_t value, unsigned nbBits);
void   FSE_flushBits(FSE_CStream_t* bitC);

void   FSE_flushCState(FSE_CStream_t* bitC, const FSE_CState_t* CStatePtr);
size_t FSE_closeCStream(FSE_CStream_t* bitC);

/*
These functions are inner components of FSE_compress_usingCTable().
They allow the creation of custom streams, mixing multiple tables and bit sources.

A key property to keep in mind is that encoding and decoding are done **in reverse direction**.
So the first symbol you will encode is the last you will decode, like a LIFO stack.

You will need a few variables to track your CStream. They are :

CTable ct;            // Provided by FSE_buildCTable()
FSE_CStream_t bitC;   // bitStream tracking structure
FSE_CState_t state;   // State tracking structure (can have several)


The first thing to do is to init bitStream and state.
    FSE_initCStream(&bitC, dstBuffer);
    FSE_initCState(&state, ct);

You can then encode your input data, byte after byte.
FSE_encodeByte() outputs a maximum of 'tableLog' bits at a time.
Remember decoding will be done in reverse direction.
    FSE_encodeByte(&bitStream, &state, symbol);

At any time, you can also add any bit sequence.
Note : maximum allowed nbBits is 25, for compatibility with 32-bits decoders
    FSE_addBits(&bitStream, bitField, nbBits);

The above methods don't commit data to memory, they just store it into local register, for speed.
Local register size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
Writing data to memory is a manual operation, performed by the flushBits function.
    FSE_flushBits(&bitStream);

Your last FSE encoding operation shall be to flush your last state value(s).
    FSE_flushState(&bitStream, &state);

Finally, you must then close the bitStream.
The function returns the size in bytes of CStream.
If there is an error, it returns an errorCode (which can be tested using FSE_isError()).
    size_t size = FSE_closeCStream(&bitStream);
*/


/******************************************
*  FSE symbol decompression API
******************************************/
typedef size_t bitD_t;

typedef struct
{
    bitD_t   bitContainer;
    unsigned bitsConsumed;
    const char* ptr;
    const char* start;
} FSE_DStream_t;

typedef struct
{
    bitD_t      state;
    const void* table;
} FSE_DState_t;


size_t FSE_initDStream(FSE_DStream_t* bitD, const void* srcBuffer, size_t srcSize);
void   FSE_initDState(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD, const DTable dt);

unsigned char FSE_decodeSymbol(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD);
bitD_t        FSE_readBits(FSE_DStream_t* bitD, unsigned nbBits);
unsigned int  FSE_reloadDStream(FSE_DStream_t* bitD);

unsigned FSE_endOfDStream(const FSE_DStream_t* bitD);
unsigned FSE_endOfDState(const FSE_DState_t* DStatePtr);

/*
Let's now decompose FSE_decompress_usingDTable() into its unitary elements.
You will decode FSE-encoded symbols from the bitStream,
and also any other bitFields you put in, **in reverse order**.

You will need a few variables to track your bitStream. They are :

FSE_DStream_t DStream;  // Stream context
FSE_DState_t DState;    // State context. Multiple ones are possible
DTable dt;              // Decoding table, provided by FSE_buildDTable()
U32 tableLog;           // Provided by FSE_readHeader()

The first thing to do is to init the bitStream.
    errorCode = FSE_initDStream(&DStream, &optionalId, srcBuffer, srcSize);

You should then retrieve your initial state(s) :
    errorCode = FSE_initDState(&DState, &DStream, DTable, tableLog);

You can then decode your data, symbol after symbol.
For information the maximum number of bits read by FSE_decodeSymbol() is 'tableLog'.
Keep in mind that symbols are decoded in reverse order, like a LIFO stack (last in, first out).
    unsigned char symbol = FSE_decodeSymbol(&DState, &DStream);

You can retrieve any bitfield you eventually stored into the bitStream (in reverse order)
Note : maximum allowed nbBits is 25
    unsigned int bitField = FSE_readBits(&DStream, nbBits);

All above operations only read from local register (which size is controlled by bitD_t==32 bits).
Refueling the register from memory is manually performed by the reload method.
    endSignal = FSE_reloadDStream(&DStream);

FSE_reloadDStream() result tells if there is still some more data to read from DStream.
0 : there is still some data left into the DStream.
1 : Dstream reached end of buffer, but is not yet fully extracted. It will not load data from memory any more.
2 : Dstream reached its exact end, corresponding in general to decompression completed.
3 : Dstream went too far. Decompression result is corrupted.

When reaching end of buffer(1), progress slowly, notably if you decode multiple symbols per loop,
to properly detect the exact end of stream.
After each decoded symbol, check if DStream is fully consumed using this simple test :
    FSE_reloadDStream(&DStream) >= 2

When it's done, verify decompression is fully completed, by checking both DStream and the relevant states.
Checking if DStream has reached its end is performed by :
    FSE_endOfDStream(&DStream);
Check also the states. There might be some entropy left there, able to decode some high probability (>50%) symbol.
    FSE_endOfDState(&DState);
*/


#if defined (__cplusplus)
}
#endif
