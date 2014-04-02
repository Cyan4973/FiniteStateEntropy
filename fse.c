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

#ifndef FSE_CFILE_1STPASS
#define FSE_CFILE_1STPASS

//****************************************************************
// Tuning parameters
//****************************************************************
// MEMORY_USAGE :
// Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
// Increasing memory usage improves compression ratio
// Reduced memory usage can improve speed, due to cache effect
// Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
#define FSE_MAX_MEMORY_USAGE 14
#define FSE_DEFAULT_MEMORY_USAGE 13

// FSE_MAX_NB_SYMBOLS :
// Maximum nb of symbol values authorized.
// Required for proper stack allocation
#define FSE_MAX_NB_SYMBOLS 286   // Suitable for zlib for example

// FSE_ILP :
// Determine if the algorithm tries to explicitly exploit ILP
// (Instruction Level Parallelism)
// Default : Recommended
#define FSE_ILP 1


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
typedef  int64_t S64;
#else
typedef unsigned char       BYTE;
typedef unsigned short      U16;
typedef   signed short      S16;
typedef unsigned int        U32;
typedef   signed int        S32;
typedef unsigned long long  U64;
typedef   signed long long  S64;
#endif

typedef size_t bitContainer_t;
typedef size_t scale_t;


//****************************************************************
//* Constants
//****************************************************************
#define FSE_MAX_NB_SYMBOLS_CHAR (FSE_MAX_NB_SYMBOLS>256 ? 256 : FSE_MAX_NB_SYMBOLS)
#define FSE_MAX_TABLELOG  (FSE_MAX_MEMORY_USAGE-2)
#define FSE_MAX_TABLESIZE (1U<<FSE_MAX_TABLELOG)
#define FSE_MAXTABLESIZE_MASK (FSE_MAX_TABLESIZE-1)
#define FSE_DEFAULT_TABLELOG (FSE_DEFAULT_MEMORY_USAGE-2)
#define FSE_MIN_TABLELOG 5

#define FSE_VIRTUAL_LOG   ((sizeof(scale_t)*8)-2)
#define FSE_VIRTUAL_RANGE ((scale_t)1<<FSE_VIRTUAL_LOG)

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

short FSE_abs(short a) { return a<0? -a : a; }


//****************************************************************
//* Header bitstream
//****************************************************************
int FSE_writeHeader (void* header, const short* normalizedCounter, int nbSymbols, int tableLog)
{
    BYTE* const ostart = (BYTE*) header;
    BYTE* out = ostart;
    int nbBits;
    const int tableSize = 1 << tableLog;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    int charnum = 0;
    int previous0 = 0;

    if (tableLog > FSE_MAX_TABLELOG) return -1;   // Unsupported
    if (tableLog < FSE_MIN_TABLELOG) return -1;   // Unsupported

    // HeaderId (normal case)
    bitStream = 2;
    bitCount  = 2;
    // Table Size
    bitStream += (tableLog-FSE_MIN_TABLELOG) << bitCount;
    bitCount  += 4;

    // Init
    remaining = tableSize+1;   // +1 for extra accuracy
    threshold = tableSize;
    nbBits = tableLog+1;

    while (remaining>1)   // stops at 1
    {
        if (previous0)
        {
            int start = charnum;
            while (!normalizedCounter[charnum]) charnum++;
            while (charnum >= start+24) { start+=24; bitStream += 0xFFFF<<bitCount; *(U16*)out=(U16)bitStream; out+=2; bitStream>>=16; }
            while (charnum >= start+3) { start+=3; bitStream += 3 << bitCount; bitCount += 2; }
            bitStream += (charnum-start) << bitCount; bitCount += 2;
            if (bitCount>16)
            {
                *(U16*)out = (U16)bitStream;
                out += 2;
                bitStream >>= 16;
                bitCount -= 16;
            }
        }
        {
            short count = normalizedCounter[charnum++];
            const short max = (short)((2*threshold-1)-remaining);
            remaining -= FSE_abs(count);
            count++;   // +1 for extra accuracy
            if (count>=threshold) count += max;   // [0..max[ [max..threshold[ (...) [threshold+max 2*threshold[
            bitStream += count << bitCount;
            bitCount  += nbBits;
            bitCount  -= (count<max);
            previous0 = (count==1);
            while (remaining<threshold) { nbBits--; threshold>>=1; }
        }
        if (bitCount>16)
        {
            *(U16*)out = (U16)bitStream;
            out += 2;
            bitStream >>= 16;
            bitCount -= 16;
        }
    }

    if (remaining<0) return -1;

    * (U16*) out = (U16) bitStream;
    out+= (bitCount+7) /8;

    if (charnum > nbSymbols) return -1;   // Too many symbols written

    return (int) (out-ostart);
}


int FSE_readHeader (short* const normalizedCounter, int* nbSymbols, int* tableLog, const void* header)
{
    const BYTE* const istart = (const BYTE*) header;
    const BYTE* ip = (const BYTE*) header;
    int nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    int charnum = 0;
    int previous0 = 0;

    bitStream = * (U32*) ip;
    bitStream >>= 2;
    nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG;   // read tableLog
    bitStream >>= 4;
    *tableLog = nbBits;
    remaining = (1<<nbBits)+1;
    threshold = 1<<nbBits;
    nbBits++;
    bitCount = 6;

    while (remaining>1)
    {
        if (previous0)
        {
            int n0 = charnum;
            while ((bitStream & 0xFFFF) == 0xFFFF) { n0+=24; ip+=2; bitStream = (*(U32*)ip) >> bitCount; }
            while ((bitStream & 3) == 3) { n0+=3; bitStream>>=2; bitCount+=2; }
            n0 += bitStream & 3; bitCount += 2;
            while (charnum < n0) normalizedCounter[charnum++] = 0;
            ip += bitCount>>3; bitCount &= 7; bitStream = (*(U32*)ip) >> bitCount;
        }
        {
            const short max = (short)((2*threshold-1)-remaining);
            short count;

            if ((bitStream & (threshold-1)) < (U32)max)
            {
                count = (short)(bitStream & (threshold-1));
                bitCount   += nbBits-1;
            }
            else
            {
                count = (short)(bitStream & (2*threshold-1));
                if (count >= threshold) count -= max;
                bitCount   += nbBits;
            }

            count--;   // extra accuracy
            remaining -= FSE_abs(count);
            normalizedCounter[charnum++] = count;
            previous0 = !count;
            while (remaining < threshold) { nbBits--; threshold >>= 1; }

            ip += bitCount>>3; bitCount &= 7; bitStream = (*(U32*)ip) >> bitCount;
        }
    }
    *nbSymbols = charnum;
    if (remaining < 1) return -1;
    if (nbBits > FSE_MAX_TABLELOG) return -1;  // Too large

    ip += bitCount>0;
    return (int) (ip-istart);
}


//****************************
// FSE Compression Code
//****************************

typedef struct
{
    int  deltaFindState;
    U16  maxState;
    BYTE minBitsOut;
} FSE_symbolCompressionTransform;

/*
CTable is a variable size structure which contains :
    U16 tableLog;
    U16 nbSymbols;
    U16 nextStateNumber[1 << tableLog];                   // This size is variable
    FSE_symbolCompressionTransform symbolTT[nbSymbols];   // This size is variable
Allocation is manual, since C standard does not support variable-size structures.
*/
#define FSE_SIZEOF_CTABLE_U32(s,t) (((((2 + (1<<t))*sizeof(U16)) + ((s+1)*sizeof(FSE_symbolCompressionTransform)))+(sizeof(U32)-1)) / sizeof(U32))
int FSE_sizeof_CTable (int nbSymbols, int tableLog)
{
    if (tableLog > FSE_MAX_TABLELOG) return -1;   // Max supported value
    return (int) (FSE_SIZEOF_CTABLE_U32 (nbSymbols, tableLog) * sizeof (U32) );
}

#define FSE_TABLESTEP(tableSize) ((tableSize>>1) + (tableSize>>3) + 3)

typedef struct
{
    U16  newState;
    BYTE symbol;
    BYTE nbBits;
} FSE_decode_t;


#define FSE_FUNCTION_TYPE BYTE
#define FSE_FUNCTION_EXTENSION
#include "fse.c"   // FSE_count, FSE_buildCTable, FSE_buildDTable, FSE_sizeof_DTable


// Emergency distribution strategy (fallback of fallback); compression will be seriously hurt ; consider increasing table size
static void FSE_emergencyDistrib(short* normalizedCounter, int nbSymbols, short points)
{
    int s=0;
    while (points)
    {
        if (normalizedCounter[s] > 1)
        {
            normalizedCounter[s]--;
            points--;
        }
        s = (s+1) % nbSymbols;
    }
}

// fallback distribution (corner case); compression will be hurt ; consider increasing table size
static void FSE_distribNpts(short* normalizedCounter, int nbSymbols, short points)
{
    int s;
    int rank[5] = {0};
    int fallback=0;

    // Sort 4 largest (they'll absorb normalization rounding)
    for (s=1; s<nbSymbols; s++)
    {
        int i, b=3;
        if (b>=s) b=s-1;
        while ((b>=0) && (normalizedCounter[s]>normalizedCounter[rank[b]])) b--;
        for (i=3; i>b; i--) rank[i+1] = rank[i];
        rank[b+1]=s;
    }

    // Distribute points
    s = 0;
    while (points)
    {
        short limit = normalizedCounter[rank[s+1]]+1;
        if (normalizedCounter[rank[s]] >= limit + points )
        {
            normalizedCounter[rank[s]] -= points;
            break;
        }
        points -= normalizedCounter[rank[s]] - limit;
        normalizedCounter[rank[s]] = limit;
        s++;
        if (s==3)
        {
            short reduction = points>>2;
            if (fallback) { FSE_emergencyDistrib(normalizedCounter, nbSymbols, points); return; }   // Fallback mode
            if (reduction < 1) reduction=1;
            if (reduction >= normalizedCounter[rank[3]]) reduction=normalizedCounter[rank[3]]-1;
            fallback = (reduction==0);
            normalizedCounter[rank[3]]-=reduction;
            points-=reduction;
            s=0;
        }
    }
}

// New faster version
int FSE_normalizeCount (short* normalizedCounter, int tableLog, unsigned int* count, int total, int nbSymbols)
{
    // Check
    if (tableLog==0) tableLog = FSE_DEFAULT_TABLELOG;
    if ((FSE_highbit(total-1)-2) < tableLog) tableLog = FSE_highbit(total-1)-2;   // Useless accuracy
    if ((FSE_highbit(nbSymbols)+1) > tableLog) tableLog = FSE_highbit(nbSymbols)+1;   // Need a minimum to represent all symbol values
    if (tableLog < FSE_MIN_TABLELOG) tableLog = FSE_MIN_TABLELOG;
    if (tableLog > FSE_MAX_TABLELOG) return -1;   // Unsupported size

    {
        U32 const rtbTable[] = {     0, 473195, 504333, 520860, 550000, 700000, 750000, 830000 };
        U64 const scale = 62 - tableLog;
        U64 const step = ((U64)1<<62) / total;   // <== (lone) division detected...
        U64 const vStep = 1ULL<<(scale-20);
        int stillToDistribute = 1<<tableLog;
        short s;
        short largest=0, largestP=0;
        U32 lowThreshold = total >> tableLog;

        for (s=0; s<nbSymbols; s++)
        {
            if (count[s] == (U32) total) return 0;   // There is only one symbol
            if (count[s]==0) { normalizedCounter[s]=0; continue; }
            if (count[s] <= lowThreshold)
            {
                normalizedCounter[s] = -1;
                stillToDistribute--;
            }
            else
            {
                short proba = (short)((count[s]*step) >> scale);
                U64 restToBeat;
                if (proba<8)
                {
                    restToBeat = vStep * rtbTable[proba];
                    proba += (count[s]*step) - ((U64)proba<<scale) > restToBeat;
                }
                if (proba > largestP) { largestP=proba; largest=s; }
                normalizedCounter[s] = proba;
                stillToDistribute -= proba;
            }
        }
        if ((int)normalizedCounter[largest] <= -stillToDistribute+8)   // largest cant accomodate that amount
            FSE_distribNpts(normalizedCounter, nbSymbols, (short)(-stillToDistribute));   // Fallback
        else normalizedCounter[largest] += (short)stillToDistribute;
    }

    /*
    {   // Print Table
        int s;
        for (s=0; s<nbSymbols; s++)
            printf("%3i: %4i \n", s, normalizedCounter[s]);
        getchar();
    }
    */

    return tableLog;
}


void* FSE_initCompressionStream(void** op)
{
    void* start = *op;
    *((BYTE**)op) += 4;   // Space to write end of bitStream
    return start;
}


void FSE_initStateAndPtrs(ptrdiff_t* state, const void** symbolTT, const void** stateTable, const void* CTable)
{
    const int tableLog = ( (U16*) CTable) [0];
    *state = (ptrdiff_t)1<<tableLog;
    *stateTable = (void*)(((const U16*) CTable) + 2);
    *symbolTT = (void*)(((const U16*)(*stateTable)) + ((ptrdiff_t)1<<tableLog));
}


void FSE_encodeByte(ptrdiff_t* state, bitStream_forward_t* bitC, BYTE symbol, const void* CTablePtr1, const void* CTablePtr2)
{
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) CTablePtr1;
    const U16* const stateTable = (const U16*) CTablePtr2;
    int nbBitsOut  = symbolTT[symbol].minBitsOut;
    nbBitsOut -= (int)((symbolTT[symbol].maxState - *state) >> 31);
    FSE_addBits(bitC, *state, nbBitsOut);
    *state = stateTable[ (*state>>nbBitsOut) + symbolTT[symbol].deltaFindState];
}


int FSE_closeCompressionStream(void* outPtr, bitStream_forward_t* bitC, void* compressionStreamDescriptor, int id)
{
    BYTE* p;
    U32 descriptor;

    FSE_flushBits(&outPtr, bitC);

    p = (BYTE*)outPtr; p += bitC->bitPos > 0;
    bitC->bitPos = 8 - bitC->bitPos; if (bitC->bitPos==8) bitC->bitPos=0;

    descriptor = (U32)(p - (BYTE*)compressionStreamDescriptor) << 3;
    descriptor += bitC->bitPos;
    descriptor += (id-1)<<30;   // optional field [1-4]
    *(U32*)compressionStreamDescriptor = descriptor;

    return (int)(p-(BYTE*)compressionStreamDescriptor);
}


int FSE_flushStates(void** outPtr, bitStream_forward_t* bitC,
                    int nbStates, ptrdiff_t state1, ptrdiff_t state2, const void* CTable)
{
    const int tableLog = ( (U16*) CTable) [0];

    if ((nbStates > 2) || (nbStates < 1)) return -1;

    if (nbStates==2) { FSE_addBits(bitC, state2, tableLog); FSE_flushBits(outPtr, bitC); }
    FSE_addBits(bitC, state1, tableLog); FSE_flushBits(outPtr, bitC);

    return 0;
}


FORCE_INLINE int FSE_compress_usingCTable_generic (void* dest, const unsigned char* source, int sourceSize, const void* CTable, int ilp)
{
    const BYTE* const istart = (const BYTE*) source;
    const BYTE* ip;
    const BYTE* const iend = istart + sourceSize;

    BYTE* op = (BYTE*) dest;
    int nbStreams = 1 + ilp;
    U32* streamSizePtr;
    ptrdiff_t state1;
    ptrdiff_t state2;
    bitStream_forward_t bitC = {0,0};   // According to C90/C99, {0} should be enough. Nonetheless, GCC complains....
    const void* stateTable;
    const void* symbolTT;


    streamSizePtr = (U32*)FSE_initCompressionStream((void**)&op);
    FSE_initStateAndPtrs(&state1, &symbolTT, &stateTable, CTable);
    state2 = state1;

    ip=iend;

    // join to even
    if (sourceSize & 1)
    {
        FSE_encodeByte(&state1, &bitC, *--ip, symbolTT, stateTable);
        FSE_flushBits((void**)&op, &bitC);
    }

    // join to mod 4 (if necessary)
    if ((sizeof(size_t)*8 > FSE_MAX_TABLELOG*4+7 ) && (sourceSize & 2))   // test bit 2
    {
        FSE_encodeByte(&state1, &bitC, *--ip, symbolTT, stateTable);
        if (ilp) FSE_encodeByte(&state2, &bitC, *--ip, symbolTT, stateTable);
        else FSE_encodeByte(&state1, &bitC, *--ip, symbolTT, stateTable);
        FSE_flushBits((void**)&op, &bitC);
    }

    // 2 or 4 per loop
    while (ip>istart)
    {
        FSE_encodeByte(&state1, &bitC, *--ip, symbolTT, stateTable);

        if (sizeof(size_t)*8 < FSE_MAX_TABLELOG*2+7 )   // this test needs to be static
            FSE_flushBits((void**)&op, &bitC);

        if (ilp) FSE_encodeByte(&state2, &bitC, *--ip, symbolTT, stateTable);
        else FSE_encodeByte(&state1, &bitC, *--ip, symbolTT, stateTable);

        if (sizeof(size_t)*8 > FSE_MAX_TABLELOG*4+7 )   // this test needs to be static
        {
            FSE_encodeByte(&state1, &bitC, *--ip, symbolTT, stateTable);

            if (ilp) FSE_encodeByte(&state2, &bitC, *--ip, symbolTT, stateTable);
            else FSE_encodeByte(&state1, &bitC, *--ip, symbolTT, stateTable);
        }

        FSE_flushBits((void**)&op, &bitC);
    }

    FSE_flushStates((void**)&op, &bitC, nbStreams, state1, state2, CTable);
    return FSE_closeCompressionStream(op, &bitC, streamSizePtr, nbStreams);
}


int FSE_compress_usingCTable (void* dest, const unsigned char* source, int sourceSize, const void* CTable)
{
    return FSE_compress_usingCTable_generic(dest, source, sourceSize, CTable, FSE_ILP);
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
    U16 tableLog;
    U16 nbSymbols;
    U16 stateTable[FSE_MAX_TABLESIZE];
    FSE_symbolCompressionTransform symbolTT[FSE_MAX_NB_SYMBOLS];   // Also used by FSE_compressU16
} CTable_max_t;

int FSE_compress2 (void* dest, const unsigned char* source, int sourceSize, int nbSymbols, int tableLog)
{
    const BYTE* const istart = (const BYTE*) source;
    const BYTE* ip = istart;

    BYTE* const ostart = (BYTE*) dest;
    BYTE* op = ostart;

    U32   count[FSE_MAX_NB_SYMBOLS_CHAR];
    S16   norm[FSE_MAX_NB_SYMBOLS_CHAR];
    CTable_max_t CTable;
    int errorCode;

    // early out
    if (sourceSize <= 1) return FSE_noCompression (ostart, istart, sourceSize);
    if (!nbSymbols) nbSymbols = FSE_MAX_NB_SYMBOLS_CHAR;
    if (!tableLog) tableLog = FSE_DEFAULT_TABLELOG;

    // Scan input and build symbol stats
    errorCode = FSE_count (count, ip, sourceSize, nbSymbols);
    if (errorCode==-1) return -1;
    if (errorCode==1) return FSE_writeSingleChar (ostart, *istart);   // Only 0 is present
    nbSymbols = errorCode;

    errorCode = FSE_normalizeCount (norm, tableLog, count, sourceSize, nbSymbols);
    if (errorCode==-1) return -1;
    if (errorCode==0) return FSE_writeSingleChar (ostart, *istart);
    tableLog = errorCode;

    // Write table description header
    errorCode = FSE_writeHeader (op, norm, nbSymbols, tableLog);
    if (errorCode==-1) return -1;
    op += errorCode;

    // Compress
    errorCode = FSE_buildCTable (&CTable, norm, nbSymbols, tableLog);
    if (errorCode==-1) return -1;
    op += FSE_compress_usingCTable (op, ip, sourceSize, &CTable);

    // check compressibility
    if ( (op-ostart) >= (sourceSize-1) ) return FSE_noCompression (ostart, istart, sourceSize);

    return (int) (op-ostart);
}


int FSE_compress (void* dest, const unsigned char* source, int sourceSize)
{
    return FSE_compress2(dest, source, sourceSize, FSE_MAX_NB_SYMBOLS_CHAR, FSE_DEFAULT_TABLELOG);
}


/*********************************************************
   Decompression (Byte symbols)
*********************************************************/
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


void FSE_updateBitStream(bitStream_backward_t* bitC, const void** ip)
{
    *((BYTE**)ip) -= bitC->bitsConsumed >> 3;
    bitC->bitContainer = * (U32*) (*ip);
    bitC->bitsConsumed &= 7;
}


FORCE_INLINE const void* FSE_initDecompressionStream_generic(
    const void** p, bitStream_backward_t* bitC, int* optionalId,
    int maxCompressedSize, int safe)
{
    const BYTE* iend;
    const BYTE* ip = (const BYTE*)*p;
    U32 descriptor;

    descriptor = * (U32*) ip;
    *optionalId = (descriptor >> 30) + 1;
    descriptor &= 0x3FFFFFFF;
    bitC->bitsConsumed = descriptor & 7;
    descriptor >>= 3;

    iend = ip + descriptor;
    if (safe) if (iend > ip+maxCompressedSize) return NULL;
    ip = iend - 4;
    *p = (const void*)ip;
    FSE_updateBitStream(bitC, p);

    return (void*)iend;
}

const void* FSE_initDecompressionStream (const void** p, bitStream_backward_t* bitC, int* optionalId)
{ return FSE_initDecompressionStream_generic(p, bitC, optionalId, 0, 0); }

const void* FSE_initDecompressionStream_safe (const void** p, bitStream_backward_t* bitC, int* optionalId, int maxCompressedSize)
{ return FSE_initDecompressionStream_generic(p, bitC, optionalId, maxCompressedSize, 1); }


void FSE_initDStates(
    int nbStates, unsigned int* state1, unsigned int* state2,
    const void** p, bitStream_backward_t* bitC,
    const int tableLog)
{
    *state1 = FSE_readBits(bitC, tableLog); FSE_updateBitStream(bitC, p);
    if (nbStates>=2) { *state2 = FSE_readBits(bitC, tableLog); FSE_updateBitStream(bitC, p); }
}


U32 FSE_readBits(bitStream_backward_t* bitC, U32 nbBits)
{
    U32 value = ((bitC->bitContainer << bitC->bitsConsumed) >> 1) >> (31-nbBits);
    bitC->bitsConsumed += nbBits;
    return value;
}


BYTE FSE_decodeSymbol(U32* state, bitStream_backward_t* bitC, const void* DTable)
{
    const FSE_decode_t* const decodeTable = (const FSE_decode_t*) DTable;
    BYTE symbol;
    U32 lowBits;
    const U32 nbBits = decodeTable[*state].nbBits;

    symbol = decodeTable[*state].symbol;
    lowBits = FSE_readBits(bitC, nbBits);
    *state = decodeTable[*state].newState + lowBits;

    return symbol;
}


int FSE_closeDecompressionStream(const void* decompressionStreamDescriptor, const void* input)
{ return (int)((BYTE*)decompressionStreamDescriptor - (BYTE*)input); }


FORCE_INLINE int FSE_decompressStreams_usingDTable_generic(
    unsigned char* dest, const int originalSize, const void* compressed, int maxCompressedSize,
    const void* DTable, const int tableLog, int safe, int nbStates)
{
    const void* ip = compressed;
    const void* iend;
    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + originalSize;
    BYTE* const olimit = oend-1;
    bitStream_backward_t bitC;
    U32 state1;
    U32 state2;

    // Init
    if (safe) iend = FSE_initDecompressionStream_safe(&ip, &bitC, &nbStates, maxCompressedSize);
    else iend = FSE_initDecompressionStream(&ip, &bitC, &nbStates);
    if (iend==NULL) return -1;
    FSE_initDStates(nbStates, &state1, &state2, &ip, &bitC, tableLog);

    // 2 symbols per loop
    while( ((safe) && ((op<olimit) && (ip>=compressed)))
        || ((!safe) && (op<olimit)) )
    {
        if (nbStates==2) *op++ = FSE_decodeSymbol(&state2, &bitC, DTable);
        else *op++ = FSE_decodeSymbol(&state1, &bitC, DTable);
        if (FSE_MAX_TABLELOG*2+7 > sizeof(U32)*8) FSE_updateBitStream(&bitC, &ip);  // Need this test to be static
        *op++ = FSE_decodeSymbol(&state1, &bitC, DTable);
        FSE_updateBitStream(&bitC, &ip);
    }

    // last symbol
    if ( ((safe) && ((op<oend) && (ip>=compressed)))
        || ((!safe) && (op<oend)) )
    {
        *op++ = FSE_decodeSymbol(&state1, &bitC, DTable);
        FSE_updateBitStream(&bitC, &ip);
    }

    if ((ip!=compressed) || bitC.bitsConsumed) return -1;   // Not fully decoded stream

    return FSE_closeDecompressionStream(iend, ip);
}

U32 FSE_getNbStates(const void* buffer)
{
    U32 descriptor = * (U32*)buffer;
    return (descriptor>>30) + 1;
}

FORCE_INLINE int FSE_decompress_usingDTable_generic(
    unsigned char* dest, const int originalSize, const void* compressed, int maxCompressedSize,
    const void* DTable, const int tableLog, int safe)
{
    U32 nbStates = FSE_getNbStates(compressed);
    if (nbStates==2)
        return FSE_decompressStreams_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, safe, 2);
    if (nbStates==1)
        return FSE_decompressStreams_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, safe, 1);
    return -1;   // should not happen
}

int FSE_decompress_usingDTable (unsigned char* dest, const int originalSize, const void* compressed, const void* DTable, const int tableLog)
{ return FSE_decompress_usingDTable_generic(dest, originalSize, compressed, 0, DTable, tableLog, 0); }

int FSE_decompress_usingDTable_safe (unsigned char* dest, const int originalSize, const void* compressed, int maxCompressedSize, const void* DTable, const int tableLog)
{ return FSE_decompress_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, 1); }


FORCE_INLINE int FSE_decompress_generic (
    unsigned char* dest, int originalSize,
    const void* compressed, int maxCompressedSize, int safe)
{
    const BYTE* const istart = (const BYTE*)compressed;
    const BYTE* ip = istart;
    short   counting[FSE_MAX_NB_SYMBOLS_CHAR];
    FSE_decode_t DTable[FSE_MAX_TABLESIZE];
    BYTE  headerId;
    int nbSymbols;
    int tableLog;
    int errorCode;

    // headerId early outs
    if ((safe) && (maxCompressedSize<2)) return -1;   // too small input size
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

int FSE_decompress (unsigned char* dest, int originalSize, const void* compressed)
{ return FSE_decompress_generic(dest, originalSize, compressed, 0, 0); }

int FSE_decompress_safe (unsigned char* dest, int originalSize, const void* compressed, int maxCompressedSize)
{ return FSE_decompress_generic(dest, originalSize, compressed, maxCompressedSize, 1); }



/*********************************************************
  U16 Compression functions
*********************************************************/

typedef struct
{
    U16  newState;
    BYTE nbBits : 4;
    U16  symbol : 12;
} FSE_decode_tU16;

#define FSE_FUNCTION_TYPE U16
#define FSE_FUNCTION_EXTENSION U16
#include "fse.c"   // FSE_countU16, FSE_buildCTableU16, FSE_buildDTableU16


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


static void FSE_encodeU16(ptrdiff_t* state, bitStream_forward_t* bitC, U16 symbol, const void* CTable1, const void* CTable2)
{
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) CTable1;
    const U16* const stateTable = (const U16*) CTable2;
    int nbBitsOut  = symbolTT[symbol].minBitsOut;
    nbBitsOut -= (int)((symbolTT[symbol].maxState - *state) >> 31);
    FSE_addBits(bitC, *state, nbBitsOut);
    *state = stateTable[ (*state>>nbBitsOut) + symbolTT[symbol].deltaFindState];
}


static int FSE_compressU16_usingCTable (void* dest, const unsigned short* source, int sourceSize, const void* CTable)
{
    const U16* const istart = (const U16*) source;
    const U16* ip;
    const U16* const iend = istart + sourceSize;

    BYTE* const ostart = (BYTE*) dest;
    BYTE* op = (BYTE*) dest;

    const int tableLog = ( (U16*) CTable) [0];
    const int tableSize = 1 << tableLog;
    const U16* const stateTable = ( (const U16*) CTable) + 2;
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) (stateTable + tableSize);


    ptrdiff_t state=tableSize;
    bitStream_forward_t bitC = {0,0};   // According to C90/C99, {0} should be enough. However, GCC complain....
    U32* streamSize = (U32*) op;
    op += 4;

    ip=iend-1;
    // cheap last-symbol storage
    state += *ip--;

    while (ip>istart+1)   // from end to beginning, up to 3 symbols at a time
    {
        FSE_encodeU16(&state, &bitC, *ip--, symbolTT, stateTable);

        if (sizeof(bitContainer_t)*8 < FSE_MAX_TABLELOG*2+7 )   // Need this test to be static
            FSE_flushBits((void**)&op, &bitC);

        FSE_encodeU16(&state, &bitC, *ip--, symbolTT, stateTable);

        if (sizeof(bitContainer_t)*8 > FSE_MAX_TABLELOG*3+7 )   // Need this test to be static
            FSE_encodeU16(&state, &bitC, *ip--, symbolTT, stateTable);

        FSE_flushBits((void**)&op, &bitC);
    }

    while (ip>=istart)   // simpler version, one symbol at a time
    {
        FSE_encodeU16(&state, &bitC, *ip--, symbolTT, stateTable);
        FSE_flushBits((void**)&op, &bitC);
    }

    // Finalize block
    FSE_addBits(&bitC, state, tableLog);
    FSE_flushBits((void**)&op, &bitC);
    *streamSize = (U32) ( ( (op- (BYTE*) streamSize) *8) + bitC.bitPos);
    op += bitC.bitPos > 0;

    return (int) (op-ostart);
}


int FSE_compressU16 (void* dest, const unsigned short* source, int sourceSize, int nbSymbols, int tableLog)
{
    const U16* const istart = source;
    const U16* ip = istart;

    BYTE* const ostart = (BYTE*) dest;
    BYTE* op = ostart;

    U32   counting[FSE_MAX_NB_SYMBOLS];
    S16   norm[FSE_MAX_NB_SYMBOLS];
    CTable_max_t CTable;


    // early out
    if (sourceSize <= 1) return FSE_noCompressionU16 (ostart, istart, sourceSize);
    if (!nbSymbols) nbSymbols = FSE_MAX_NB_SYMBOLS;
    if (!tableLog) tableLog = FSE_DEFAULT_TABLELOG;

    // Scan for stats
    nbSymbols = FSE_countU16 (counting, ip, sourceSize, nbSymbols);
    if (nbSymbols==1) return FSE_writeSingleU16(ostart, *istart);

    // Normalize
    tableLog = FSE_normalizeCount (norm, tableLog, counting, sourceSize, nbSymbols);
    if (tableLog==0) return FSE_writeSingleU16(ostart, *istart);

    op += FSE_writeHeader (op, norm, nbSymbols, tableLog);

    // Compress
    FSE_buildCTableU16 (&CTable, norm, nbSymbols, tableLog);
    op += FSE_compressU16_usingCTable (op, ip, sourceSize, &CTable);

    // check compressibility
    if ( (size_t)(op-ostart) >= (size_t)(sourceSize-1)*(sizeof(short)) )
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

U16 FSE_decodeSymbolU16(U32* state, U32 bitStream, int* bitsConsumed, const void* DTable)
{
    const FSE_decode_tU16* const decodeTable = (const FSE_decode_tU16*) DTable;
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
    bitStream_backward_t bitC;
    U32 state;

    // Init
    bitC.bitsConsumed = ( ( (* (U32*) ip)-1) & 7) + 1 + 24;
    iend = ip + ( ( (* (U32*) ip) +7) / 8);
    ip = iend - 4;
    bitC.bitContainer = * (U32*) ip;

    bitC.bitsConsumed = 32 - bitC.bitsConsumed;
    state = (bitC.bitContainer << bitC.bitsConsumed) >> (32-tableLog);
    bitC.bitsConsumed += tableLog;

    FSE_updateBitStream(&bitC, (const void**)&ip);

    // Hot loop
    while (op<oend-1)
    {
        *op++ = FSE_decodeSymbolU16(&state, bitC.bitContainer, &bitC.bitsConsumed, DTable);
        if ((sizeof(U32)*8 > FSE_MAX_TABLELOG*2+7) && (sizeof(void*)==8))   // Need this test to be static
            *op++ = FSE_decodeSymbolU16(&state, bitC.bitContainer, &bitC.bitsConsumed, DTable);
        FSE_updateBitStream(&bitC, (const void**)&ip);
    }
    if (op<oend) *(oend-1) = FSE_decodeSymbolU16(&state, bitC.bitContainer, &bitC.bitsConsumed, DTable);

    // cheap last symbol storage
    *oend = (U16) state;

    return (int) (iend- (const BYTE*) compressed);
}


int FSE_decompressU16(unsigned short* dest, int originalSize,
                    const void* compressed)
{
    const BYTE* const istart = (const BYTE*) compressed;
    const BYTE* ip = istart;
    short   counting[FSE_MAX_NB_SYMBOLS];
    FSE_decode_tU16 DTable[FSE_MAX_TABLESIZE];
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


#else   // FSE_CFILE_1STPASS

/*
  2nd part of the file
  designed to be auto-included repetitively
  for type-specific functions (template equivalent in C)
  Objective is to write such functions only once, for better maintenance
*/

// checks
#ifndef FSE_FUNCTION_EXTENSION
#  error "FSE_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSE_FUNCTION_TYPE
#  error "FSE_FUNCTION_TYPE must be defined"
#endif

// Function names
#define FSE_CAT(X,Y) X##Y
#define FSE_FUNCTION_NAME(X,Y) FSE_CAT(X,Y)
#define FSE_TYPE_NAME(X,Y) FSE_CAT(X,Y)

// Functions
int FSE_FUNCTION_NAME(FSE_count, FSE_FUNCTION_EXTENSION) (unsigned int* count, const FSE_FUNCTION_TYPE* source, int sourceSize, int maxNbSymbols)
{
    const FSE_FUNCTION_TYPE* ip = (const FSE_FUNCTION_TYPE*) source;
    const FSE_FUNCTION_TYPE* const iend = ip+sourceSize;
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

    while (!count[maxNbSymbols-1]) maxNbSymbols--;
    return maxNbSymbols;
}

int FSE_FUNCTION_NAME(FSE_buildCTable, FSE_FUNCTION_EXTENSION)
(void* CTable, const short* normalizedCounter, int nbSymbols, int tableLog)
{
    const int tableSize = 1 << tableLog;
    const int tableMask = tableSize - 1;
    U16* tableU16 = ( (U16*) CTable) + 2;
    FSE_symbolCompressionTransform* symbolTT = (FSE_symbolCompressionTransform*) (tableU16 + tableSize);
    const int step = FSE_TABLESTEP(tableSize);
    int cumul[FSE_MAX_NB_SYMBOLS+1];
    U32 position = 0;
    FSE_FUNCTION_TYPE tableSymbol[FSE_MAX_TABLESIZE];
    U32 highThreshold = tableSize-1;
    int s;
    int i;

    // header
    tableU16[-2] = (U16) tableLog;
    tableU16[-1] = (U16) nbSymbols;

    // For explanations on how to distribute symbol values over the table :
    // http://fastcompression.blogspot.fr/2014/02/fse-distributing-symbol-values.html

    // symbol start positions
    cumul[0] = 0;
    for (i=1; i<=nbSymbols; i++)
    {
        if (normalizedCounter[i-1]==-1)   // Low prob symbol
        {
            cumul[i] = cumul[i-1] + 1;
            tableSymbol[highThreshold--] = (FSE_FUNCTION_TYPE)(i-1);
        }
        else
        cumul[i] = cumul[i-1] + normalizedCounter[i-1];
    }
    cumul[nbSymbols] = tableSize+1;

    // Spread symbols
    for (s=0; s<nbSymbols; s++)
    {
        int i;
        for (i=0; i<normalizedCounter[s]; i++)
        {
            tableSymbol[position] = (FSE_FUNCTION_TYPE)s;
            position = (position + step) & tableMask;
            if (position > highThreshold) position = (position + step) & tableMask;   // Lowprob area
        }
    }

    if (position!=0) return -1;   // Must have gone through all positions, otherwise normalizedCount is not correct

    // Build table
    for (i=0; i<tableSize; i++)
    {
        FSE_FUNCTION_TYPE s = tableSymbol[i];
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
            case -1:
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


#define FSE_DECODE_TYPE FSE_TYPE_NAME(FSE_decode_t, FSE_FUNCTION_EXTENSION)

int FSE_FUNCTION_NAME(FSE_sizeof_DTable, FSE_FUNCTION_EXTENSION) (int tableLog)
{ return (int) ( (1<<tableLog) * (int) sizeof (FSE_DECODE_TYPE) ); }

int FSE_FUNCTION_NAME(FSE_buildDTable, FSE_FUNCTION_EXTENSION)
(void* DTable, const short* const normalizedCounter, int nbSymbols, int tableLog)
{
    FSE_DECODE_TYPE* const tableDecode = (FSE_DECODE_TYPE*) DTable;
    const U32 tableSize = 1 << tableLog;
    const U32 tableMask = tableSize-1;
    const U32 step = FSE_TABLESTEP(tableSize);
    U16 symbolNext[FSE_MAX_NB_SYMBOLS];
    U32 position = 0;
    U32 highThreshold = tableSize-1;
    int s;

    // Checks
    if (nbSymbols > FSE_MAX_NB_SYMBOLS) return -1;
    if (tableLog > FSE_MAX_TABLELOG) return -1;

    // Low prob symbols
    for (s=0; s<nbSymbols; s++)
        if (normalizedCounter[s]==-1)
            tableDecode[highThreshold--].symbol = (FSE_FUNCTION_TYPE)s;

    // Spread symbols
    for (s=0; s<nbSymbols; s++)
    {
        int i;
        for (i=0; i<normalizedCounter[s]; i++)
        {
            tableDecode[position].symbol = (FSE_FUNCTION_TYPE)s;
            position = (position + step) & tableMask;
            if (position > highThreshold) position = (position + step) & tableMask;   // lowprob area
        }
    }

    if (position!=0) return -1;   // position must use all positions, otherwise normalizedCounter is incorrect

    // Calculate symbol next
    for (s=0; s<nbSymbols; s++) symbolNext[s] = FSE_abs(normalizedCounter[s]);

    // Build table Decoding table
    {
        U32 i;
        for (i=0; i<tableSize; i++)
        {
            FSE_FUNCTION_TYPE s = tableDecode[i].symbol;
            U16 nextState = symbolNext[s]++;
            tableDecode[i].nbBits = (BYTE) (tableLog - FSE_highbit (nextState) );
            tableDecode[i].newState = (U16) ( (nextState << tableDecode[i].nbBits) - tableSize);
        }
    }

    return 0;
}

// remove definitions
#undef FSE_FUNCTION_EXTENSION
#undef FSE_FUNCTION_TYPE
#undef FSE_CAT
#undef FSE_FUNCTION_NAME
#undef FSE_TYPE_NAME
#undef FSE_DECODE_TYPE

#endif   // FSE_CFILE_1STPASS



