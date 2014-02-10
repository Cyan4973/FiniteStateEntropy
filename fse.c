/* ******************************************************************
   FSE : Finite State Entropy coder
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


//****************************************************************
// Tuning parameters
//****************************************************************
// MEMORY_USAGE :
// Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
// Increasing memory usage improves compression ratio
// Reduced memory usage can improve speed, due to cache effect
// Default value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
#define FSE_MEMORY_USAGE 14

// FSE_MAX_NB_SYMBOLS :
// Maximum nb of symbol values authorized.
// Required for proper stack allocation
#define FSE_MAX_NB_SYMBOLS 286   // Suitable for zlib for example


//****************************************************************
//* Includes
//****************************************************************
#include "fse.h"
#include <stddef.h>    // ptrdiff_t
#include <string.h>    // memcpy, memset
#include <stdio.h>     // printf (debug)


//****************************************************************
//* Basic Types
//****************************************************************
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   // C99
# include <stdint.h>
typedef  uint8_t BYTE;
typedef uint16_t U16;
typedef  int16_t S16;
typedef uint32_t U32;
typedef  int32_t S32;
typedef uint64_t U64;
#else
typedef unsigned char       BYTE;
typedef unsigned short      U16;
typedef   signed short      S16;
typedef unsigned int        U32;
typedef   signed int        S32;
typedef unsigned long long  U64;
#endif

typedef size_t bitContainer_t;


//****************************************************************
//* Constants
//****************************************************************
#define FSE_MAX_NB_SYMBOLS_CHAR (FSE_MAX_NB_SYMBOLS>256 ? 256 : FSE_MAX_NB_SYMBOLS)
#define FSE_MAX_TABLELOG  (FSE_MEMORY_USAGE-2)
#define FSE_MAX_TABLESIZE (1U<<FSE_MAX_TABLELOG)
#define FSE_MAXTABLESIZE_MASK (FSE_MAX_TABLESIZE-1)
#define FSE_MIN_TABLELOG 5

#define FSE_VIRTUAL_LOG   30
#define FSE_VIRTUAL_RANGE (1U<<FSE_VIRTUAL_LOG)

#if FSE_MAX_TABLELOG>15
#error "FSE_MAX_TABLELOG>15 isn't supported"
#endif


//****************************************************************
//* Compiler specifics
//****************************************************************
#ifdef _MSC_VER    // Visual Studio
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    // For Visual 2005
#  pragma warning(disable : 4127)        // disable: C4127: conditional expression is constant
#  pragma warning(disable : 4214)        // disable: C4214: non-int bitfields
#else
#  define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#  ifdef __GNUC__
#    define FORCE_INLINE static inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE static inline
#  endif
#endif


/****************************************************************
  Internal functions
****************************************************************/
FORCE_INLINE int FSE_highbit (register U32 val)
{
#   if defined(_MSC_VER)   // Visual
    unsigned long r;
    _BitScanReverse ( &r, val );
    return (int) r;
#   elif defined(__GNUC__) && (GCC_VERSION >= 304)   // GCC Intrinsic
    return 31 - __builtin_clz (val);
#   else   // Software version
    static const int DeBruijnClz[32] = { 0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30, 8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31 };
    U32 v = val;
    int r;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    r = DeBruijnClz[ (U32) (v * 0x07C4ACDDU) >> 27];
    return r;
#   endif
}


//****************************************************************
//* Header bitstream
//****************************************************************
int FSE_writeHeader (void* header, const unsigned int* normalizedCounter, int nbSymbols, int tableLog)
{
    BYTE* const ostart = (BYTE*) header;
    BYTE* out = ostart;
    int nbBits = tableLog;
    const int tableSize = 1 << nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    int charnum = 0;

    if (nbBits > FSE_MAX_TABLELOG) return -1;   // Unsupported
    if (nbBits < FSE_MIN_TABLELOG) return -1;   // Unsupported

    // HeaderId (normal case)
    bitStream = 2;
    bitCount  = 2;
    // Table Size
    bitStream += (nbBits-FSE_MIN_TABLELOG) <<bitCount;
    bitCount  += 4;

    // Init
    remaining = tableSize;
    threshold = tableSize;
    nbBits++;

    while (remaining>0)
    {
        int count = normalizedCounter[charnum++];
        const int max = (2*threshold-1)-remaining;
        remaining -= count;
        if (count>=threshold) count += max;   // [0..max[ [max..threshold[ (...) [threshold+max 2*threshold[
        bitStream += count << bitCount;
        bitCount  += nbBits;
        bitCount  -= (count<max);
        if (bitCount>16)
        {
            *(U16*)out = (U16)bitStream;
            out += 2;
            bitStream >>= 16;
            bitCount -= 16;
        }
        while (remaining<threshold) { nbBits--; threshold>>=1; }
    }

    if (remaining<0) return -1;

    * (U16*) out = (U16) bitStream;
    out+= (bitCount+7) /8;

    if (charnum > nbSymbols) return -1;   // Too many symbols written

    return (int) (out-ostart);
}


int FSE_readHeader (unsigned int* const normalizedCounter, int* nbSymbols, int* tableLog, const void* header)
{
    const BYTE* const istart = (const BYTE*) header;
    const BYTE* ip = (const BYTE*) header;
    int nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    int charnum = 0;

    bitStream = * (U32*) ip;
    bitStream >>= 2;
    nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG;   // read memLog
    bitStream >>= 4;
    *tableLog = nbBits;
    remaining = (1<<nbBits);
    threshold = remaining;
    nbBits++;
    bitCount = 6;

    while (remaining>0)
    {
        const U32 max = (2*threshold-1)-remaining;
        int count;

        if ((bitStream & (threshold-1)) < max)
        {
            count = bitStream & (threshold-1);
            bitCount   += nbBits-1;
        }
        else
        {
            count = bitStream & (2*threshold-1);
            if (count >= threshold) count -= max;
            bitCount   += nbBits;
        }

        remaining -= count;
        normalizedCounter[charnum++] = count;
        while (remaining < threshold) { nbBits--; threshold >>= 1; }

        ip += bitCount>>3;
        bitCount &= 7;
        bitStream = (*(U32*)ip) >> bitCount;
    }
    *nbSymbols = charnum;
    if (remaining < 0) return -1;
    if (nbBits > FSE_MAX_TABLELOG) return -1;  // Too large

    ip += bitCount>0;
    return (int) (ip-istart);
}


//****************************
// FSE Compression Code
//****************************

int FSE_count (unsigned int* count, const unsigned char* source, int sourceSize, int maxNbSymbols)
{
    const BYTE* ip = (const BYTE*) source;
    const BYTE* const iend = ip+sourceSize;
    int   i;

    U32   Counting1[FSE_MAX_NB_SYMBOLS_CHAR] = {0};
    U32   Counting2[FSE_MAX_NB_SYMBOLS_CHAR] = {0};
    U32   Counting3[FSE_MAX_NB_SYMBOLS_CHAR] = {0};
    U32   Counting4[FSE_MAX_NB_SYMBOLS_CHAR] = {0};

    // Init checks
    if (maxNbSymbols > FSE_MAX_NB_SYMBOLS_CHAR) return -1;        // maxNbSymbols too large : unsupported
    if (!maxNbSymbols) maxNbSymbols = FSE_MAX_NB_SYMBOLS_CHAR;    // 0: default
    if (!sourceSize) return -1;                                   // Error : no input

    while (ip < iend-3)
    {
        Counting1[*ip++]++;
        Counting2[*ip++]++;
        Counting3[*ip++]++;
        Counting4[*ip++]++;
    }
    while (ip<iend) Counting1[*ip++]++;

    for (i=0; i<maxNbSymbols; i++) count[i] = Counting1[i] + Counting2[i] + Counting3[i] + Counting4[i];

    while (!count[maxNbSymbols-1]) maxNbSymbols--;
    return maxNbSymbols;
}


int FSE_normalizeCount (unsigned int* normalizedCounter, int tableLog, unsigned int* count, int total, int nbSymbols)
{
    int vTotal= total;

    // Check
    if ((FSE_highbit(total-1)+1) < tableLog) tableLog = FSE_highbit(total-1)+1;   // Useless accuracy
    if ((FSE_highbit(nbSymbols)+1) > tableLog) tableLog = FSE_highbit(nbSymbols-1)+1;   // Need a minimum to represent all symbol values
    if (tableLog < FSE_MIN_TABLELOG) tableLog = FSE_MIN_TABLELOG;
    if (tableLog > FSE_MAX_TABLELOG) return -1;   // Unsupported size

    // Ensure proper scale
    {
        int const maxLog = FSE_VIRTUAL_LOG - tableLog;
        int srcLog = FSE_highbit(total-1)+1;
        if (srcLog > maxLog)
        {
            const int shift = srcLog - maxLog;
            const int base = (1<<shift)-1;
            int s;
            vTotal=0;
            for (s=0; s<nbSymbols; s++) vTotal += count[s] = (count[s]+base) >> shift;
        }
    }

    // Ensure minimum step is 1
    if (total > (1<<tableLog))
    {
        U32 minBase, add;
        int s;
        minBase = total;
        add = (minBase * nbSymbols) >> tableLog;
        do { minBase += add; add = (add * nbSymbols) >> tableLog; } while (add);
        minBase >>= tableLog;
        for (s=0; s<nbSymbols; s++)
        {
            if (count[s])
            {
                normalizedCounter[s] = count[s] + minBase;
                vTotal += minBase;
            }
        }
    }
    {
        U32 const scale = FSE_VIRTUAL_LOG - tableLog;
        U32 const vStep = 1 << scale;
        U32 const step = FSE_VIRTUAL_RANGE / vTotal;   // OK, here we have a (lone) division...
        U32 const error = FSE_VIRTUAL_RANGE - (step * vTotal);   // >= 0
        int s;
        int cumulativeRest = ((int)vStep + error) >> 1;
        if (error > vStep) cumulativeRest = error;     // Note : in this case, total is too large; Error will be given to first non-zero symbol

        for (s=0; s<nbSymbols; s++)
        {
            if (normalizedCounter[s]== (U32) vTotal) return 0; // There is only one symbol
            if (count[s]>0)
            {
                int rest;
                U32 size = (normalizedCounter[s]*step) >> scale;
                rest = (normalizedCounter[s]*step) - (size * vStep);   // necessarily >= 0
                cumulativeRest += rest;
                size += cumulativeRest >> scale;
                cumulativeRest &= vStep-1;
                normalizedCounter[s] = size;
            }
        }
    }

    return tableLog;
}


typedef struct
{
    int  deltaFindState;
    U16  maxState;
    BYTE minBitsOut;
} FSE_symbolCompressionTransform;

/*
CTable is a variable size structure which contains :
    U16 memLog;
    U16 nbSymbols;
    U16 nextStateNumber[1 << memLog];                     // This size is variable
    FSE_symbolCompressionTransform symbolTT[nbSymbols];   // This size is variable
Allocation is fully manual, since C standard does not support variable-size structures.
*/
#define FSE_SIZEOF_CTABLE_U32(s,t) (((((2 + (1<<t))*sizeof(U16)) + (s*sizeof(FSE_symbolCompressionTransform)))+(sizeof(U32)-1)) / sizeof(U32))
int FSE_sizeof_CTable (int nbSymbols, int tableLog)
{
    if (tableLog > FSE_MAX_TABLELOG) return 0;   // Max supported value
    return (int) (FSE_SIZEOF_CTABLE_U32 (nbSymbols, tableLog) * sizeof (U32) );
}

#define FSE_TABLESTEP(tableSize) ((tableSize>>1) + (tableSize>>3) + 3)
int FSE_buildCTable (void* CTable, const unsigned int* normalizedCounter, int nbSymbols, int tableLog)
{
    const int tableSize = 1 << tableLog;
    const int tableMask = tableSize - 1;
    U16* tableU16 = ( (U16*) CTable) + 2;
    FSE_symbolCompressionTransform* symbolTT = (FSE_symbolCompressionTransform*) (tableU16 + tableSize);
    const int step = FSE_TABLESTEP(tableSize);
    int cumul[FSE_MAX_NB_SYMBOLS_CHAR+1];
    U32 position = 0;
    BYTE tableSymbolByte[FSE_MAX_TABLESIZE];
    int s;
    int i;

    // header
    tableU16[-2] = (U16) tableLog;
    tableU16[-1] = (U16) nbSymbols;

    // symbol start positions
    cumul[0] = 0;
    for (i=1; i<nbSymbols; i++) cumul[i] = cumul[i-1] + normalizedCounter[i-1];
    cumul[nbSymbols] = tableSize+1;

    // Spread symbols
    for (s=0; s<nbSymbols; s++)
    {
        U32 i;
        for (i=0; i<normalizedCounter[s]; i++)
        {
            tableSymbolByte[position] = (BYTE)s;
            position = (position + step) & tableMask;
        }
    }

    if (position!=0) return -1;   // Must have gone through all positions, otherwise normalizedCount is not correct

    // Build table
    for (i=0; i<tableSize; i++)
    {
        BYTE s = tableSymbolByte[i];
        tableU16[cumul[s]++] = (U16) (tableSize+i);
    }

    // Build Symbol Transformation Table
    {
        int s;
        int total = 0;
        for (s=0; s<nbSymbols; s++)
        {
            switch (normalizedCounter[s])
            {
            case 0:
                break;
            case 1:
                symbolTT[s].minBitsOut = (BYTE) tableLog;
                symbolTT[s].deltaFindState = total - 1;
                total ++;
                symbolTT[s].maxState = (U16) ( (tableSize*2) - 1); // ensures state <= maxState
                break;
            default :
                symbolTT[s].minBitsOut = (BYTE) ( (tableLog-1) - FSE_highbit (normalizedCounter[s]-1) );
                symbolTT[s].deltaFindState = total - normalizedCounter[s];
                total +=  normalizedCounter[s];
                symbolTT[s].maxState = (U16) ( (normalizedCounter[s]<< (symbolTT[s].minBitsOut+1) ) - 1);
            }
        }
    }

    return 0;
}


void* FSE_initCompressionStream(void** op, ptrdiff_t* state, const void** symbolTT, const void** stateTable, const void* CTable)
{
    void* start = *op;
    const int memLog = ( (U16*) CTable) [0];
    *((BYTE**)op) += 4;
    *state = ((ptrdiff_t)1<<memLog);
    *stateTable = (void*)(((const U16*) CTable) + 2);
    *symbolTT = (void*)(((const U16*)(*stateTable)) + ((ptrdiff_t)1<<memLog));
    return start;
}


void FSE_encodeByte(ptrdiff_t* state, size_t* bitStream, int* bitpos, BYTE symbol, const void* CTable1, const void* CTable2)
{
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) CTable1;
    const U16* const stateTable = (const U16*) CTable2;
    int nbBitsOut  = symbolTT[symbol].minBitsOut;
    nbBitsOut -= (int)((symbolTT[symbol].maxState - *state) >> 31);
    FSE_addBits(bitStream, bitpos, nbBitsOut, *state);
    *state = stateTable[ (*state>>nbBitsOut) + symbolTT[symbol].deltaFindState];
}


int   FSE_closeCompressionStream(size_t bitStream, int bitPos, ptrdiff_t state, void* op, void* compressionStreamDescriptor, const void* CTable)
{
    const int memLog = ( (U16*) CTable) [0];
    BYTE* p;

    FSE_addBits(&bitStream, &bitPos, memLog, state);
    FSE_flushBits(&bitStream, &op, &bitPos);

    p = (BYTE*)op;
    *(U32*)compressionStreamDescriptor = (U32) ( ( (p - (BYTE*)compressionStreamDescriptor) *8) + bitPos);
    p += bitPos>0;
    return (int)(p-(BYTE*)compressionStreamDescriptor);
}


int FSE_compress_usingCTable (void* dest, const unsigned char* source, int sourceSize, const void* CTable)
{
    const BYTE* const istart = (const BYTE*) source;
    const BYTE* ip;
    const BYTE* const iend = istart + sourceSize;

    BYTE* op = (BYTE*) dest;
    U32* streamSizePtr;
    ptrdiff_t state;
    bitContainer_t bitStream=0;
    int bitpos=0;
    const void* stateTable;
    const void* symbolTT;


    streamSizePtr = (U32*)FSE_initCompressionStream((void**)&op, &state, &symbolTT, &stateTable, CTable);

    ip=iend-1;
    state += *ip--;   // cheap last-symbol storage (assumption : nbSymbols <= 1<<tableLog)

    while (ip>istart+1)   // from end to beginning, up to 3 bytes at a time
    {
        FSE_encodeByte(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);

        if (sizeof(bitContainer_t)*8 < FSE_MAX_TABLELOG*2+7 )   // this test needs to be static
            FSE_flushBits(&bitStream, (void**)&op, &bitpos);

        FSE_encodeByte(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);

        if (sizeof(bitContainer_t)*8 > FSE_MAX_TABLELOG*3+7 )   // this test needs to be static
            FSE_encodeByte(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);

        FSE_flushBits(&bitStream, (void**)&op, &bitpos);
    }

    while (ip>=istart)   // simpler version, one symbol at a time
    {
        FSE_encodeByte(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);
        FSE_flushBits(&bitStream, (void**)&op, &bitpos);
    }

    return FSE_closeCompressionStream(bitStream, bitpos, state, op, streamSizePtr, CTable);
}


int FSE_writeSingleChar (BYTE *out, BYTE symbol)
{
    *out++=1;     // Header means ==> 1 symbol repeated across the whole sequence
    *out=symbol;
    return 2;
}

int FSE_noCompression (BYTE* out, const BYTE* in, int isize)
{
    *out++=0;     // Header means ==> uncompressed
    memcpy (out, in, isize);
    return (isize+1);
}


typedef struct
{
    U16 memLog;
    U16 nbSymbols;
    U16 stateTable[FSE_MAX_TABLESIZE];
    FSE_symbolCompressionTransform symbolTT[FSE_MAX_NB_SYMBOLS];   // Also used by FSE_compressU16
} CTable_max_t;

int FSE_compress2 (void* dest, const unsigned char* source, int sourceSize, int nbSymbols, int memLog)
{
    const BYTE* const istart = (const BYTE*) source;
    const BYTE* ip = istart;

    BYTE* const ostart = (BYTE*) dest;
    BYTE* op = ostart;

    U32   counting[FSE_MAX_NB_SYMBOLS_CHAR];
    CTable_max_t CTable;
    int errorCode;

    // early out
    if (sourceSize <= 1) return FSE_noCompression (ostart, istart, sourceSize);
    if (!nbSymbols) nbSymbols = FSE_MAX_NB_SYMBOLS_CHAR;
    if (!memLog) memLog = FSE_MAX_TABLELOG;

    // Scan input and build symbol stats
    errorCode = FSE_count (counting, ip, sourceSize, nbSymbols);
    if (errorCode==-1) return -1;
    if (errorCode==1) return FSE_writeSingleChar (ostart, *istart);   // Only 0 is present
    nbSymbols = errorCode;

    errorCode = FSE_normalizeCount (counting, memLog, counting, sourceSize, nbSymbols);
    if (errorCode==-1) return -1;
    if (errorCode==0) return FSE_writeSingleChar (ostart, *istart);
    memLog = errorCode;

    // Write table description header
    errorCode = FSE_writeHeader (op, counting, nbSymbols, memLog);
    if (errorCode==-1) return -1;
    op += errorCode;

    // Compress
    errorCode = FSE_buildCTable (&CTable, counting, nbSymbols, memLog);
    if (errorCode==-1) return -1;
    op += FSE_compress_usingCTable (op, ip, sourceSize, &CTable);

    // check compressibility
    if ( (op-ostart) >= (sourceSize-1) ) return FSE_noCompression (ostart, istart, sourceSize);

    return (int) (op-ostart);
}


int FSE_compress (void* dest, const unsigned char* source, int sourceSize)
{
    return FSE_compress2(dest, source, sourceSize, FSE_MAX_NB_SYMBOLS_CHAR, FSE_MAX_TABLELOG);
}


/*********************************************************
   Decompression (Byte symbols)
*********************************************************/
typedef struct
{
    U16  newState;
    BYTE symbol;
    BYTE nbBits;
} FSE_decode_t;


int FSE_sizeof_DTable (int memLog)
{
    return (int) ( (1<<memLog) * (int) sizeof (FSE_decode_t) );
}

int FSE_buildDTable (void* DTable, const unsigned int* const normalizedCounter, int nbSymbols, int tableLog)
{
    FSE_decode_t* const tableDecode = (FSE_decode_t*) DTable;
    const U32 tableSize = 1 << tableLog;
    const U32 tableMask = tableSize-1;
    const U32 step = FSE_TABLESTEP(tableSize);
    U32 symbolNext[FSE_MAX_NB_SYMBOLS_CHAR];
    U32 position = 0;
    int s;

    // Checks
    if (nbSymbols > FSE_MAX_NB_SYMBOLS_CHAR) return -1;
    if (tableLog > FSE_MAX_TABLELOG) return -1;

    // Spread symbols
    for (s=0; s<nbSymbols; s++)
    {
        U32 i;
        for (i=0; i<normalizedCounter[s]; i++)
        {
            tableDecode[position].symbol = (BYTE)s;
            position = (position + step) & tableMask;
        }
    }

    if (position!=0) return -1;   // position must use all positions, otherwise normalizedCounter is incorrect

    // Calculate symbol next
    for (s=0; s<nbSymbols; s++) symbolNext[s] = normalizedCounter[s];   // Objective : don't modify normalizedCounter

    // Build table Decoding table
    {
        U32 i;
        for (i=0; i<tableSize; i++)
        {
            BYTE s = tableDecode[i].symbol;
            U32 nextState = symbolNext[s]++;
            tableDecode[i].nbBits = (BYTE) (tableLog - FSE_highbit (nextState) );
            tableDecode[i].newState = (U16) ( (nextState << tableDecode[i].nbBits) - tableSize);
        }
    }

    return 0;
}


int FSE_decompressRaw (void* out, int osize, const BYTE* in)
{
    memcpy (out, in+1, osize);
    return osize+1;
}

int FSE_decompressSingleSymbol (void* out, int osize, const BYTE symbol)
{
    memset (out, symbol, osize);
    return 2;
}


FORCE_INLINE const void* FSE_initDecompressionStream_generic(
    const void** p, int* bitsConsumed, unsigned int* state, unsigned int* bitStream,
    const int tableLog, int maxCompressedSize, int safe)
{
    const BYTE* iend;
    const BYTE* ip = (const BYTE*)*p;

    *bitsConsumed = ( ( (* (U32*) ip)-1) & 7) + 1 + 24;
    iend = ip + ( ( (* (U32*) ip) +7) / 8);
    if (safe) if (iend > ip+maxCompressedSize) return NULL;
    ip = iend - 4;
    *bitStream = * (U32*) ip;

    *bitsConsumed = 32-*bitsConsumed;
    *state = (*bitStream << *bitsConsumed) >> (32-tableLog);
    *bitsConsumed += tableLog;

    FSE_updateBitStream(bitStream, bitsConsumed, (const void**)&ip);
    *p = (void*)ip;
    return (void*)iend;
}


const void* FSE_initDecompressionStream(const void** p, int* bitsConsumed, unsigned int* state, unsigned int* bitStream, const int tableLog)
{
    return FSE_initDecompressionStream_generic(p, bitsConsumed, state, bitStream, tableLog, 0, 0);
}


const void* FSE_initDecompressionStream_safe(const void** p, int* bitsConsumed, unsigned int* state, unsigned int* bitStream, const int tableLog, int maxCompressedSize)
{
    return FSE_initDecompressionStream_generic(p, bitsConsumed, state, bitStream, tableLog, maxCompressedSize, 1);
}


U32 FSE_readBits(int* bitsConsumed, U32 bitStream, int nbBits)
{
    U32 value = ((bitStream << *bitsConsumed) >> 1) >> (31-nbBits);
    *bitsConsumed += nbBits;
    return value;
}


BYTE FSE_decodeSymbol(U32* state, int* bitsConsumed, U32 bitStream, const void* DTable)
{
    const FSE_decode_t* const decodeTable = (const FSE_decode_t*) DTable;
    U32 rest;
    BYTE symbol;
    const int nbBits = decodeTable[*state].nbBits;

    symbol = decodeTable[*state].symbol;
    rest = FSE_readBits(bitsConsumed, bitStream, nbBits);
    *state = decodeTable[*state].newState + rest;

    return symbol;
}


void FSE_updateBitStream(U32* bitStream, int* bitsConsumed, const void** ip)
{
    *((BYTE**)ip) -= *bitsConsumed >> 3;
    *bitStream = * (U32*) (*ip);
    *bitsConsumed &= 7;
}


int FSE_closeDecompressionStream(const void* decompressionStreamDescriptor, const void* input)
{
    return (int)((BYTE*)decompressionStreamDescriptor - (BYTE*)input);
}


FORCE_INLINE int FSE_decompress_usingDTable_generic(
    unsigned char* dest, const int originalSize, const void* compressed, int maxCompressedSize,
    const void* DTable, const int tableLog, int safe)
{
    const void* ip = compressed;
    const void* iend;
    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + originalSize - 1;
    U32 bitStream;
    int bitCount;
    U32 state;

    // Init
    if (safe) iend = FSE_initDecompressionStream_safe(&ip, &bitCount, &state, &bitStream, tableLog, maxCompressedSize);
    else iend = FSE_initDecompressionStream(&ip, &bitCount, &state, &bitStream, tableLog);
    if (iend==NULL) return -1;

    // Hot loop
    //while ((op<oend-1) && (ip>=compressed))
    //while (op<oend-1)
    while( ((safe) && ((op<oend-1) && (ip>=compressed)))
        || ((!safe) && (op<oend-1)) )
    {
        *op++ = FSE_decodeSymbol(&state, &bitCount, bitStream, DTable);
        if ((sizeof(U32)*8 > FSE_MAX_TABLELOG*2+7) && (sizeof(void*)==8))   // Need this test to be static
            *op++ = FSE_decodeSymbol(&state, &bitCount, bitStream, DTable);
        FSE_updateBitStream(&bitStream, &bitCount, &ip);
    }
    if (op<oend) *(oend-1) = FSE_decodeSymbol(&state, &bitCount, bitStream, DTable);
    FSE_updateBitStream(&bitStream, &bitCount, &ip);

    // cheap last symbol storage
    *oend = (BYTE) state;

    if ((ip!=compressed) || bitCount) return -1;   // Not fully decoded stream

    return FSE_closeDecompressionStream(iend, ip);
}


int FSE_decompress_usingDTable (unsigned char* dest, const int originalSize, const void* compressed, const void* DTable, const int tableLog)
{
    return FSE_decompress_usingDTable_generic(dest, originalSize, compressed, 0, DTable, tableLog, 0);
}

int FSE_decompress_usingDTable_safe (unsigned char* dest, const int originalSize, const void* compressed, int maxCompressedSize, const void* DTable, const int tableLog)
{
    return FSE_decompress_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, 1);
}


FORCE_INLINE int FSE_decompress_generic (
    unsigned char* dest, int originalSize,
    const void* compressed, int maxCompressedSize, int safe)
{
    const BYTE* const istart = (const BYTE*)compressed;
    const BYTE* ip = istart;
    U32   counting[FSE_MAX_NB_SYMBOLS_CHAR];
    FSE_decode_t DTable[FSE_MAX_TABLESIZE];
    BYTE  headerId;
    int nbSymbols;
    int tableLog;
    int errorCode;

    // headerId early outs
    headerId = ip[0] & 3;
    if (ip[0]==0) return FSE_decompressRaw (dest, originalSize, istart);
    if (ip[0]==1) return FSE_decompressSingleSymbol (dest, originalSize, istart[1]);
    if (headerId!=2) return -1;   // unused headerId

    // normal FSE decoding mode
    errorCode = FSE_readHeader (counting, &nbSymbols, &tableLog, istart);
    if (errorCode==-1) return -1;
    ip += errorCode;

    errorCode = FSE_buildDTable (DTable, counting, nbSymbols, tableLog);
    if (errorCode==-1) return -1;

    if (safe) errorCode = FSE_decompress_usingDTable_safe (dest, originalSize, ip, maxCompressedSize, DTable, tableLog);
    else errorCode = FSE_decompress_usingDTable (dest, originalSize, ip, DTable, tableLog);
    if (errorCode==-1) return -1;
    ip += errorCode;

    return (int) (ip-istart);
}


int FSE_decompress (unsigned char* dest, int originalSize,
                    const void* compressed)
{
    return FSE_decompress_generic(dest, originalSize, compressed, 0, 0);
    //return FSE_decompress_generic(dest, originalSize, compressed, originalSize, 1);   // for tests
}

int FSE_decompress_safe (unsigned char* dest, int originalSize,
                    const void* compressed, int maxCompressedSize)
{
    return FSE_decompress_generic(dest, originalSize, compressed, maxCompressedSize, 1);
}

/*********************************************************
  U16 Compression functions
*********************************************************/
static int FSE_countU16 (unsigned int* count, const unsigned short* source, int sourceSize, int maxNbSymbols)
{
    const U16* ip = source;
    const U16* const iend = ip+sourceSize;
    int   i;

    U32   Counting1[FSE_MAX_NB_SYMBOLS] = {0};
    U32   Counting2[FSE_MAX_NB_SYMBOLS] = {0};
    U32   Counting3[FSE_MAX_NB_SYMBOLS] = {0};
    U32   Counting4[FSE_MAX_NB_SYMBOLS] = {0};

    // Init checks
    if (maxNbSymbols > FSE_MAX_NB_SYMBOLS) return -1;        // maxNbSymbols too large : unsupported
    if (!maxNbSymbols) maxNbSymbols = FSE_MAX_NB_SYMBOLS;    // 0: default
    if (!sourceSize) return -1;                              // Error : no input

    while (ip < iend-3)
    {
        Counting1[*ip++]++;
        Counting2[*ip++]++;
        Counting3[*ip++]++;
        Counting4[*ip++]++;
    }
    while (ip<iend) Counting1[*ip++]++;

    for (i=0; i<maxNbSymbols; i++) count[i] = Counting1[i] + Counting2[i] + Counting3[i] + Counting4[i];

    {
        int max = maxNbSymbols;
        while (!count[max-1]) max--;
        return max;
    }
}


static int FSE_buildCTableU16 (void* CTable, const unsigned int* normalizedCounter, int nbSymbols, int tableLog)
{
    const int tableSize = 1 << tableLog;
    const int tableMask = tableSize - 1;
    U16* tableU16 = ( (U16*) CTable) + 2;
    FSE_symbolCompressionTransform* const symbolTT = (FSE_symbolCompressionTransform*) (tableU16 + tableSize);
    const int step = FSE_TABLESTEP(tableSize);
    int symbolPos[FSE_MAX_NB_SYMBOLS+1];
    U32 position = 0;
    U16 tableSymbolU16[FSE_MAX_TABLESIZE];
    U16 s;
    int i;

    // header
    tableU16[-2] = (U16) tableLog;
    tableU16[-1] = (U16) nbSymbols;

    // symbol start positions
    symbolPos[0] = 0;
    for (i=1; i<nbSymbols; i++)
    {
        symbolPos[i] = symbolPos[i-1] + normalizedCounter[i-1];
    }
    symbolPos[nbSymbols] = tableSize+1;

    // Spread symbols
    s=0;
    while (!symbolPos[s+1]) s++;
    for (i=0; i<tableSize; i++)
    {
        tableSymbolU16[position] = s;
        while (i+2 > symbolPos[s+1])
            s++;
        position = (position + step) & tableMask;
    }

    // Build table
    for (i=0; i<tableSize; i++)
    {
        U16 s = tableSymbolU16[i];
        tableU16[symbolPos[s]] = (U16) (tableSize+i);
        symbolPos[s]++;
    }

    // Build Symbol Transformation Table
    {
        int s;
        int total = 0;
        for (s=0; s<nbSymbols; s++)
        {
            switch (normalizedCounter[s])
            {
            case 0:
                break;
            case 1:
                symbolTT[s].minBitsOut = (BYTE) tableLog;
                symbolTT[s].deltaFindState = total - 1;
                total ++;
                symbolTT[s].maxState = (U16) ( (tableSize*2) - 1);   // ensures state <= maxState
                break;
            default :
                symbolTT[s].minBitsOut = (BYTE) ( (tableLog-1) - FSE_highbit (normalizedCounter[s]-1) );
                symbolTT[s].deltaFindState = total - normalizedCounter[s];
                total +=  normalizedCounter[s];
                symbolTT[s].maxState = (U16) ( (normalizedCounter[s]<< (symbolTT[s].minBitsOut+1) ) - 1);
            }
        }
    }

    return 0;
}


static int FSE_noCompressionU16(void* dest, const U16* source, int sourceSize)
{
    BYTE* header = (BYTE*)dest;
    *header=0;
    memcpy(header+1, source, sourceSize*2);
    return sourceSize*2 + 1;
}


static int FSE_writeSingleU16(void* dest, U16 value)
{
    BYTE* header = (BYTE*) dest;
    U16* valueP16 = (U16*)(header+1);
    *header=1;
    *valueP16 = value;
    return 3;
}


static void FSE_encodeU16(ptrdiff_t* state, size_t* bitStream, int* bitpos, U16 symbol, const void* CTable1, const void* CTable2)
{
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) CTable1;
    const U16* const stateTable = (const U16*) CTable2;
    int nbBitsOut  = symbolTT[symbol].minBitsOut;
    nbBitsOut -= (int)((symbolTT[symbol].maxState - *state) >> 31);
    FSE_addBits(bitStream, bitpos, nbBitsOut, *state);
    *state = stateTable[ (*state>>nbBitsOut) + symbolTT[symbol].deltaFindState];
}


static int FSE_compressU16_usingCTable (void* dest, const unsigned short* source, int sourceSize, const void* CTable)
{
    const U16* const istart = (const U16*) source;
    const U16* ip;
    const U16* const iend = istart + sourceSize;

    BYTE* const ostart = (BYTE*) dest;
    BYTE* op = (BYTE*) dest;

    const int memLog = ( (U16*) CTable) [0];
    const int tableSize = 1 << memLog;
    const U16* const stateTable = ( (const U16*) CTable) + 2;
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) (stateTable + tableSize);


    ptrdiff_t state=tableSize;
    int bitpos=0;
    bitContainer_t bitStream=0;
    U32* streamSize = (U32*) op;
    op += 4;

    ip=iend-1;
    // cheap last-symbol storage
    state += *ip--;

    while (ip>istart+1)   // from end to beginning, up to 3 symbols at a time
    {
        FSE_encodeU16(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);

        if (sizeof(bitContainer_t)*8 < FSE_MAX_TABLELOG*2+7 )   // Need this test to be static
            FSE_flushBits(&bitStream, (void**)&op, &bitpos);

        FSE_encodeU16(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);

        if (sizeof(bitContainer_t)*8 > FSE_MAX_TABLELOG*3+7 )   // Need this test to be static
            FSE_encodeU16(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);

        FSE_flushBits(&bitStream, (void**)&op, &bitpos);
    }

    while (ip>=istart)   // simpler version, one symbol at a time
    {
        FSE_encodeU16(&state, &bitStream, &bitpos, *ip--, symbolTT, stateTable);
        FSE_flushBits(&bitStream, (void**)&op, &bitpos);
    }

    // Finalize block
    FSE_addBits(&bitStream, &bitpos, memLog, state);
    FSE_flushBits(&bitStream, (void**)&op, &bitpos);
    *streamSize = (U32) ( ( (op- (BYTE*) streamSize) *8) + bitpos);
    op += bitpos>0;

    return (int) (op-ostart);
}


int FSE_compressU16 (void* dest, const unsigned short* source, int sourceSize, int nbSymbols, int memLog)
{
    const U16* const istart = source;
    const U16* ip = istart;

    BYTE* const ostart = (BYTE*) dest;
    BYTE* op = ostart;

    U32   counting[FSE_MAX_NB_SYMBOLS];
    CTable_max_t CTable;


    // early out
    if (sourceSize <= 1) return FSE_noCompressionU16 (ostart, istart, sourceSize);
    if (!nbSymbols) nbSymbols = FSE_MAX_NB_SYMBOLS;
    if (!memLog) memLog = FSE_MAX_TABLELOG;

    // Scan for stats
    nbSymbols = FSE_countU16 (counting, ip, sourceSize, nbSymbols);
    if (nbSymbols==1) return FSE_writeSingleU16(ostart, *istart);

    // Normalize
    memLog = FSE_normalizeCount (counting, memLog, counting, sourceSize, nbSymbols);
    if (memLog==0) return FSE_writeSingleU16(ostart, *istart);

    op += FSE_writeHeader (op, counting, nbSymbols, memLog);

    // Compress
    FSE_buildCTableU16 (&CTable, counting, nbSymbols, memLog);
    op += FSE_compressU16_usingCTable (op, ip, sourceSize, &CTable);

    // check compressibility
    if ( (op-ostart) >= (sourceSize-1) )
        return FSE_noCompressionU16 (ostart, istart, sourceSize);

    return (int) (op-ostart);
}


/*********************************************************
   U16 Decompression functions
*********************************************************/
int FSE_decompressRawU16 (U16* out, int osize, const BYTE* in)
{
    memcpy (out, in+1, osize*2);
    return osize*2+1;
}

int FSE_decompressSingleU16 (U16* out, int osize, U16 value)
{
    int i;
    for (i=0; i<osize; i++) *out++ = value;
    return 3;
}


typedef struct
{
    U16  newState;
    BYTE nbBits : 4;
    U16  symbol : 12;
} FSE_decodeU16_t;


int FSE_buildDTableU16 (void* DTable, const unsigned int* const normalizedCounter, int nbSymbols, int tableLog)
{
    FSE_decodeU16_t* const tableDecode = (FSE_decodeU16_t*) DTable;
    const int tableSize = 1 << tableLog;
    const int tableMask = tableSize-1;
    const U32 step = FSE_TABLESTEP(tableSize);
    U32 symbolNext[FSE_MAX_NB_SYMBOLS];
    U32 position = 0;
    int s, i;

    // Checks
    if (nbSymbols > FSE_MAX_NB_SYMBOLS) return -1;

    // symbol start positions
    symbolNext[0] = normalizedCounter[0];
    for (s=1; s<nbSymbols; s++)
    {
        symbolNext[s] = symbolNext[s-1] + normalizedCounter[s];
    }

    // Spread symbols
    s=0;
    while (!symbolNext[s]) s++;
    for (i=0; i<tableSize; i++)
    {
        tableDecode[position].symbol = (U16) s;
        while ( (U32) i+2 > symbolNext[s]) s++;
        position = (position + step) & tableMask;
    }

    // Calculate symbol next
    for (s=0; s<nbSymbols; s++) symbolNext[s] = normalizedCounter[s];

    // Build table Decoding table
    {
        int i;
        for (i=0; i<tableSize; i++)
        {
            U16 s = tableDecode[i].symbol;
            U32 nextState = symbolNext[s]++;
            tableDecode[i].nbBits = (BYTE) (tableLog - FSE_highbit (nextState) );
            tableDecode[i].newState = (U16) ( (nextState << tableDecode[i].nbBits) - tableSize);
        }
    }

    return 0;
}


U16 FSE_decodeSymbolU16(U32* state, U32 bitStream, int* bitsConsumed, const void* DTable)
{
    const FSE_decodeU16_t* const decodeTable = (const FSE_decodeU16_t*) DTable;
    U32 rest;
    U16 symbol;
    const int nbBits = decodeTable[*state].nbBits;

    symbol = decodeTable[*state].symbol;

    rest = ( (bitStream << *bitsConsumed) >> 1) >> (31 - nbBits);  // faster than mask
    *bitsConsumed += nbBits;

    *state = decodeTable[*state].newState + rest;

    return symbol;
}


int FSE_decompressU16_usingDTable (unsigned short* dest, const int originalSize, const void* compressed, const void* DTable, const int tableLog)
{
    const BYTE* ip = (const BYTE*) compressed;
    const BYTE* iend;
    U16* op = dest;
    U16* const oend = op + originalSize - 1;
    U32 bitStream;
    int bitCount;
    U32 state;

    // Init
    bitCount = ( ( (* (U32*) ip)-1) & 7) + 1 + 24;
    iend = ip + ( ( (* (U32*) ip) +7) / 8);
    ip = iend - 4;
    bitStream = * (U32*) ip;

    bitCount = 32-bitCount;
    state = (bitStream << bitCount) >> (32-tableLog);
    bitCount += tableLog;

    FSE_updateBitStream(&bitStream, &bitCount, (const void**)&ip);

    // Hot loop
    while (op<oend-1)
    {
        *op++ = FSE_decodeSymbolU16(&state, bitStream, &bitCount, DTable);
        if ((sizeof(U32)*8 > FSE_MAX_TABLELOG*2+7) && (sizeof(void*)==8))   // Need this test to be static
            *op++ = FSE_decodeSymbolU16(&state, bitStream, &bitCount, DTable);
        FSE_updateBitStream(&bitStream, &bitCount, (const void**)&ip);
    }
    if (op<oend) *(oend-1) = FSE_decodeSymbolU16(&state, bitStream, &bitCount, DTable);

    // cheap last symbol storage
    *oend = (U16) state;

    return (int) (iend- (const BYTE*) compressed);
}


int FSE_decompressU16(unsigned short* dest, int originalSize,
                    const void* compressed)
{
    const BYTE* const istart = (const BYTE*) compressed;
    const BYTE* ip = istart;
    U32   counting[FSE_MAX_NB_SYMBOLS];
    FSE_decodeU16_t DTable[FSE_MAX_TABLESIZE];
    BYTE  headerId;
    int nbSymbols;
    int tableLog;

    // headerId early outs
    headerId = ip[0] & 3;
    if (headerId==0) return FSE_decompressRawU16 (dest, originalSize, istart);
    if (headerId==1) return FSE_decompressSingleU16 (dest, originalSize, *(U16*)(istart+1));

    // normal FSE decoding mode
    ip += FSE_readHeader (counting, &nbSymbols, &tableLog, istart);
    FSE_buildDTableU16 (DTable, counting, nbSymbols, tableLog);
    ip += FSE_decompressU16_usingDTable (dest, originalSize, ip, DTable, tableLog);

    return (int) (ip-istart);
}


