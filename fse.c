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

// FSE_DEBUG
// Enable verification code, which checks table construction and state values (much slower, for debug purpose only)
#define FSE_DEBUG 0


//****************************************************************
//* Includes
//****************************************************************
#include "fse.h"
#include <string.h>    // memcpy, memset
#include <stdio.h>     // printf (debug)


//****************************************************************
//* Basic Types
//****************************************************************
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   // C99
# include <stdint.h>
  typedef  uint8_t BYTE;
  typedef uint16_t U16;
  typedef uint32_t U32;
  typedef  int32_t S32;
  typedef uint64_t U64;
#else
  typedef unsigned char       BYTE;
  typedef unsigned short      U16;
  typedef unsigned int        U32;
  typedef   signed int        S32;
  typedef unsigned long long  U64;
#endif

typedef U32 bitContainer_t;


//****************************************************************
//* Constants
//****************************************************************
#define FSE_MAX_NB_SYMBOLS 256
#define FSE_MAX_TABLELOG  (FSE_MEMORY_USAGE-2)
#define FSE_MAX_TABLESIZE (1U<<FSE_MAX_TABLELOG)
#define FSE_MAXTABLESIZE_MASK (FSE_MAX_TABLESIZE-1)
#define FSE_MIN_TABLELOG 5

#define FSE_VIRTUAL_LOG   30
#define FSE_VIRTUAL_RANGE (1U<<FSE_VIRTUAL_LOG)
#define FSE_VIRTUAL_SCALE (FSE_VIRTUAL_LOG-FSE_MAX_TABLELOG)
#define FSE_VIRTUAL_STEP  (1U << FSE_VIRTUAL_SCALE)

#if FSE_MAX_TABLELOG>15
#error "FSE_MAX_TABLELOG>15 isn't supported"
#endif

#if FSE_DEBUG
static long long nbBlocks = 0;     // debug
static long long toCheck  = -1;    // debug
static long long nbDBlocks = 0;    // debug
#endif


//****************************************************************
//* Compiler specifics
//****************************************************************
#ifdef _MSC_VER    // Visual Studio
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    // For Visual 2005
#  pragma warning(disable : 4127)        // disable: C4127: conditional expression is constant
#else 
#  define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#  ifdef __GNUC__
#    define FORCE_INLINE static inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE static inline
#  endif
#endif


//****************************************************************
//* Internal functions
//****************************************************************
FORCE_INLINE int FSE_highbit (register U32 val)
{
#   if defined(_MSC_VER)   // Visual
    unsigned long r;
    _BitScanReverse( &r, val );
    return (int)r;
#   elif defined(__GNUC__) && (GCC_VERSION >= 304)   // GCC Intrinsic
    return 31 - __builtin_clz(val);
#   else   // Software version
    static const int DeBruijnClz[32] = { 0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30, 8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31 };
    U32 v = val;
    int r;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    r = DeBruijnClz[(U32)(v * 0x07C4ACDDU) >> 27];
    return r;
#   endif
}


//****************************************************************
//* Header bitstream
//****************************************************************
static int FSE_writeSingleSymbolHeader (BYTE *out, BYTE symbol)
{
    *out++=1;     // Header means ==> 1 symbol repeated across the whole sequence
    *out=symbol;
    return 2;
}



int FSE_writeHeader(void* header, const unsigned int* normalizedCounter, int nbSymbols, int tableLog)
{
    BYTE* const ostart = (BYTE*)header;
    BYTE* out = ostart;
    int nbBits = tableLog;
    const int tableSize = 1 << nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    BYTE charnum = 0;
    int bitSent = 0;

    if (nbBits > FSE_MAX_TABLELOG) return -1;   // Unsupported
    if (nbBits < FSE_MIN_TABLELOG) return -1;   // Unsupported

    // HeaderId (normal case)
    bitStream = 2;
    bitCount  = 2;
    // Table Size
    bitStream += (nbBits-FSE_MIN_TABLELOG)<<bitCount;
    bitCount  += 4;

    // Init
    remaining = tableSize;
    threshold = tableSize;
    nbBits++;

    while (remaining)
    {
        int count = normalizedCounter[charnum++];
        const int margin = ((threshold*2-1) - remaining) & 0xFFFFFFFE;   // Need even number to not influence last bit
        if (count==tableSize) return FSE_writeSingleSymbolHeader((BYTE*)header, charnum-1);   // quick end : only one symbol value
        remaining -= count;
        if (count<margin) count += margin * (remaining ? normalizedCounter[charnum] & 1 : 0);
        else count += margin;
        bitStream += (count >> bitSent) << bitCount;
        bitCount += nbBits - bitSent;
        bitSent = count < 2*margin;
        if (bitCount>16) { *(U16*)out = (U16)bitStream; out+=2; bitStream>>=16; bitCount-=16; }
        while (remaining<threshold) { nbBits--; threshold>>=1; }
    }
    *(U16*)out = (U16)bitStream; out+=(bitCount+7)/8;

    if (charnum > nbSymbols) return -1;   // Too many symbols written

    return (int)(out-ostart);
}


int FSE_readHeader (unsigned int* const normalizedCounter, int* nbSymbols, int* tableLog, const void* header)
{
    const BYTE* const istart = (const BYTE*)header;
    const BYTE* ip = (const BYTE*)header;
    int nbBits;
    int remaining;
    U32 mask;
    int threshold;
    U32 bitStream;
    int bitCount;
    int charnum = 0;
    int bitSent = 0;
    int bitValue = 0;

    bitStream = *(U32*)ip; ip+=4;
    bitStream >>= 2; bitCount = 30;                  // removes 2-bit headerId
    nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG;   // read memLog
    bitStream >>= 4; bitCount -= 4;
    *tableLog = nbBits;
    remaining = (1<<nbBits);
    threshold = remaining; nbBits++;
    mask = 2*threshold-1;

    while (remaining)
    {
        const int readMask = mask >> bitSent;
        int count = ((bitStream & readMask) << bitSent) + bitValue;
        const int margin = ((2*threshold-1) - remaining) & 0xFFFFFFFE;
        bitCount -= (nbBits-bitSent);
        bitStream >>= (nbBits-bitSent);
        if (count>=2*margin) { bitSent=0; bitValue=0; count -= margin; }
        else { bitSent=1; bitValue = (count >= margin); count -= margin*bitValue; }
        remaining -= count;
        normalizedCounter[charnum++] = count;
        if (bitCount<nbBits) { bitStream += (*(U16*)ip) << bitCount; ip+=2; bitCount+= 16;}
        while (remaining<threshold) { nbBits--; threshold >>= 1; mask >>= 1; }
    }
    *nbSymbols = charnum;

    ip -= bitCount>>3;   // realign
    return (int)(ip-istart);
}


//****************************
// FSE Compression Code
//****************************

int FSE_count(unsigned int* count, const void* source, int sourceSize, int maxNbSymbols)
{
    const BYTE* ip = (const BYTE*)source;
    const BYTE* const iend = ip+sourceSize;
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

    { int max = maxNbSymbols; while (!count[max-1]) max--; return max; }
}


int FSE_normalizeCount(unsigned int* normalizedCounter, int tableLog, unsigned int* count, int total, int nbSymbols)
{
    int vTotal= total;

#if FSE_DEBUG
    U32 countOrig[MAX_NB_SYMBOLS] = {0};
    { int s; for (s=0; s<nbSymbols; s++) countOrig[s]=count[s+2]; }
#endif

    // Check
    if (tableLog > FSE_MAX_TABLELOG) return -1;   // Unsupported size

    {
        // Ensure minimum step is 1
        U32 const minBase = (total + ((total*nbSymbols) >> tableLog) + (((total*nbSymbols) >> tableLog)*nbSymbols >> tableLog)) >> tableLog;
        int s;
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
        U32 const step = FSE_VIRTUAL_RANGE / vTotal;   // OK, here we have a (lone) division...
        U32 const error = FSE_VIRTUAL_RANGE - (step * vTotal);   // >= 0
        int s;
        int cumulativeRest = ((int)FSE_VIRTUAL_STEP + error) / 2;
        
        if (error > FSE_VIRTUAL_STEP) cumulativeRest = error;   // Note : in this case, total is too large; Error will be given to first non-zero symbol

        for (s=0; s<nbSymbols; s++)
        {
            if (normalizedCounter[s]==(U32)vTotal) return s+1;   // There is only one symbol
            if (count[s]>0)
            {
                int rest;
                U32 size = (normalizedCounter[s]*step) >> FSE_VIRTUAL_SCALE;
                rest = (normalizedCounter[s]*step) - (size * FSE_VIRTUAL_STEP);   // necessarily >= 0
                cumulativeRest += rest;
                size += cumulativeRest >> FSE_VIRTUAL_SCALE;
                cumulativeRest &= (FSE_VIRTUAL_STEP - 1);
                normalizedCounter[s] = size;
            }
        }
    }

#if FSE_DEBUG
    {
        int localtotal = 0;
        int s;
        for (s=0; s<nbSymbols; s++) 
            if ((normalizedCounter[s]==0) && (countOrig[s]))
            {
                U32 const minBase = (total + ((total*nbSymbols) >> tableLog) + (((total*nbSymbols) >> tableLog)*nbSymbols >> tableLog)) >> tableLog;
                printf("Error : symbol %i has been nullified (%i/%i => added %i) \n", s, countOrig[s], total, minBase);
            }
        for (s=0; s<nbSymbols; s++) localtotal += count[s];
        if (localtotal != 1 << tableLog) 
        {
            U32 const step = FSE_VIRTUAL_RANGE / vTotal;
            U32 const error = FSE_VIRTUAL_RANGE - (step * vTotal);
            printf("Bad Table Count => %i != %i  \n", total, 1 << tableLog);
            printf("step size : %i  (vsize = %i)\n", step, vTotal);
            printf("Eval final error : %i \n", error);
            printf("\n");
        }
    }
#endif

    return 0;
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
int FSE_sizeof_CTable(int nbSymbols, int tableLog)
{
    if (tableLog > FSE_MAX_TABLELOG) return 0;   // Max supported value
    return (int)(FSE_SIZEOF_CTABLE_U32(nbSymbols, tableLog) * sizeof(U32));
}

#define FSE_TABLESTEP(tableSize) ((tableSize>>1) + (tableSize>>3) + 3)
int FSE_buildCTable(void* CTable, const unsigned int* normalizedCounter, int nbSymbols, int tableLog)
{
    const int tableSize = 1 << tableLog;
    const int tableMask = tableSize - 1;
    U16* tableU16 = ((U16*)CTable) + 2;
    FSE_symbolCompressionTransform* symbolTT = (FSE_symbolCompressionTransform*)(tableU16 + tableSize);
    const int step = FSE_TABLESTEP(tableSize);
    int symbolPos[FSE_MAX_NB_SYMBOLS+1];
    U32 position = 0;
    BYTE tableBYTE[FSE_MAX_TABLESIZE] = {0};
    BYTE s;
    int i;

    // header
    tableU16[-2] = (U16)tableLog;
    tableU16[-1] = (U16)nbSymbols;

    // symbol start positions
    symbolPos[0] = 0;
    for (i=1; i<nbSymbols; i++) { symbolPos[i] = symbolPos[i-1] + normalizedCounter[i-1]; }
    symbolPos[nbSymbols] = tableSize+1;

    // Spread symbols
    s=0;
    while (!symbolPos[s+1]) s++;
    for (i=0; i<tableSize; i++)
    {
        tableBYTE[position] = s;
        while (i+2 > symbolPos[s+1])
            s++;
        position = (position + step) & tableMask;
    }

    // Build table
    for (i=0; i<tableSize; i++)
    {
        BYTE s = tableBYTE[i];
        tableU16[symbolPos[s]] = (U16)(tableSize+i);
        symbolPos[s]++;
    }

#if FSE_DEBUG
    {
        // Check table
        int verif[FSE_MAX_TABLESIZE] = {0};
        int s;

        for (s=0; s<nbSymbols; s++)
        {
            int nb=0;
            for (i=0; i<tableSize; i++) if (tableBYTE[i]==s) nb++;
            if ((U32)nb != normalizedCounter[s]) 
                printf("FSE_buildTable_rotation : Count pb for symbol %i : %i present (%i should be)\n", s, nb, normalizedCounter[s]);
        }

        for (i=0; i<tableSize; i++)
        {
            int state = tableU16[i] - tableSize;
            if ((state<0) ||
                (state>tableSize) ||
                (verif[state]))
            {
                printf("FSE_buildTable_rotation : Table problem (at pos %i, symbol %i) !! => state %i not correct (allocated?)\n", i, tableBYTE[i], state);
            }
            verif[state]=1;
        }
    }
#endif

    // Build Symbol Transformation Table
    {
        int s;
        int total = 0;
        for (s=0; s<nbSymbols; s++)
        {
            switch (normalizedCounter[s])
            {
            case 0: break;
            case 1:
                symbolTT[s].minBitsOut = (BYTE)tableLog;
                symbolTT[s].deltaFindState = total - 1;
                total ++;
                symbolTT[s].maxState = (U16)((tableSize*2) - 1);   // ensures state <= maxState
                break;
            default :
                symbolTT[s].minBitsOut = (BYTE)((tableLog-1) - FSE_highbit(normalizedCounter[s]-1));
                symbolTT[s].deltaFindState = total - normalizedCounter[s];
                total +=  normalizedCounter[s];
                symbolTT[s].maxState = (U16)((normalizedCounter[s]<<(symbolTT[s].minBitsOut+1)) - 1);
            }
        }
    }

    return 0;
}

int FSE_compress_usingCTable (void* dest, const void* source, int sourceSize, void* CTable)
{
    const BYTE* const istart = (const BYTE*)source;
    const BYTE* ip;
    const BYTE* const iend = istart + sourceSize;

    BYTE* const ostart = (BYTE*)dest;
    BYTE* op = (BYTE*)dest;

    const int memLog = ((U16*)CTable)[0];
    const int tableSize = 1 << memLog;
    U16* const stateTable = ((U16*)CTable) + 2;
    FSE_symbolCompressionTransform* const symbolTT = (FSE_symbolCompressionTransform*) (stateTable + tableSize);

    const U32 mask[] = { 0, 1, 3, 7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF, 0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF, 0x1FFFFFF};   // up to 25 bits

    int state=tableSize;
    int bitpos=0;
    bitContainer_t bitStream=0;
    U32* streamSize = (U32*)op; op += 4;

    ip=iend-1;
    // cheap last-symbol storage
    state += *ip--;

    while (ip>istart)   // from end to beginning, 2 bytes at a time
    {
        const BYTE symbol  = *ip--;
        const BYTE symbol2 = *ip--;
        int nbBitsOut  = symbolTT[symbol].minBitsOut;
        int nbBitsOut2 = symbolTT[symbol2].minBitsOut;

        nbBitsOut += (state > symbolTT[symbol].maxState);
        bitStream += (state & mask[nbBitsOut]) << bitpos;
        bitpos += nbBitsOut;

        state = stateTable[(state>>nbBitsOut) + symbolTT[symbol].deltaFindState];
#  if FSE_MAX_TABLELOG>12
        *(bitContainer_t*)op = bitStream; { int nbBytes = bitpos >> 3; bitpos &= 7; op += nbBytes; bitStream >>= nbBytes*8; }
#  endif

        nbBitsOut2 += (state > symbolTT[symbol2].maxState);
        bitStream += (state & mask[nbBitsOut2]) << bitpos;
        bitpos += nbBitsOut2;

        state = stateTable[(state>>nbBitsOut2) + symbolTT[symbol2].deltaFindState];
        *(bitContainer_t*)op = bitStream; { int nbBytes = bitpos >> 3; bitpos &= 7; op += nbBytes; bitStream >>= nbBytes*8; }   // Better speed on 32-bits when done before state update
    }

    while (ip>=istart)   // simpler version, one symbol at a time
    {
        const BYTE symbol  = *ip--;
        int nbBitsOut  = symbolTT[symbol].minBitsOut;
        nbBitsOut += (state > symbolTT[symbol].maxState);
        bitStream += (state & mask[nbBitsOut]) << bitpos;
        bitpos += nbBitsOut;
        state = stateTable[(state>>nbBitsOut) + symbolTT[symbol].deltaFindState];
        *(bitContainer_t*)op = bitStream; { int nbBytes = bitpos/8; bitpos &= 7; op += nbBytes; bitStream >>= nbBytes*8; }
    }

    // Finalize block
    bitStream += (state & mask[memLog]) << bitpos;
    bitpos += memLog;
    *(bitContainer_t*)op = bitStream;
    *streamSize = (U32)(((op-(BYTE*)streamSize)*8) + bitpos);
    op += (bitpos+7)/8;

    return (int)(op-ostart);
}


static int FSE_noCompression (BYTE* out, const BYTE* in, int isize)
{
    *out++=0;     // Header means ==> uncompressed
    memcpy(out, in, isize);
    return (isize+1);
}


typedef struct {
    U16 memLog;
    U16 nbSymbols;
    U16 stateTable[FSE_MAX_TABLESIZE];
    FSE_symbolCompressionTransform symbolTT[FSE_MAX_NB_SYMBOLS];
} CTable_max_t;

int FSE_compress_Nsymbols (void* dest, const void* source, int sourceSize, int nbSymbols)
{
    const BYTE* const istart = (const BYTE*)source;
    const BYTE* ip = istart;

    BYTE* const ostart = (BYTE*)dest;
    BYTE* op = ostart;

    U32   counting[FSE_MAX_NB_SYMBOLS];
    CTable_max_t CTable;   // [FSE_SIZEOF_CTABLE_U32(MAX_NB_SYMBOLS, FSE_MAX_TABLELOG)];


    // early out
    if (sourceSize <= 1) return FSE_noCompression(ostart, istart, sourceSize);

    // Scan for stats
    nbSymbols = FSE_count(counting, ip, sourceSize, nbSymbols);

    // Normalize
    {
        int s1 = FSE_normalizeCount(counting, FSE_MAX_TABLELOG, counting, sourceSize, nbSymbols);
        if (s1) return FSE_writeSingleSymbolHeader(ostart, (BYTE)(s1-1));   // only one symbol in the set
    }

    op += FSE_writeHeader(op, counting, nbSymbols, FSE_MAX_TABLELOG);

    // Compress
    FSE_buildCTable(&CTable, counting, nbSymbols, FSE_MAX_TABLELOG);
    op += FSE_compress_usingCTable(op, ip, sourceSize, &CTable);

    // check compressibility
    if ((op-ostart)>=(sourceSize-1))
        return FSE_noCompression(ostart, istart, sourceSize);

    return (int)(op-ostart);
}


int FSE_compress (void* dest, const void* source, int sourceSize) { return FSE_compress_Nsymbols(dest, source, sourceSize, FSE_MAX_NB_SYMBOLS); }


//****************************
// Decompression CODE
//****************************

typedef struct
{
    U16  newState;
    BYTE symbol;
    BYTE nbBits;
} FSE_decode_t;


int FSE_sizeof_DTable(int memLog) { return (int)((1<<memLog)*(int)sizeof(FSE_decode_t)); }

int FSE_buildDTable(void* DTable, const unsigned int* const normalizedCounter, int nbSymbols, int tableLog)
{
    FSE_decode_t* const tableDecode = (FSE_decode_t*)DTable;
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
    for (s=1; s<nbSymbols; s++) { symbolNext[s] = symbolNext[s-1] + normalizedCounter[s]; }

    // Spread symbols
    s=0;
    while (!symbolNext[s]) s++;
    for (i=0; i<tableSize; i++)
    {
        tableDecode[position].symbol = (BYTE)s;
        while ((U32)i+2 > symbolNext[s]) s++;
        position = (position + step) & tableMask;
    }

    // Calculate symbol next
    for (s=0; s<nbSymbols; s++) symbolNext[s] = normalizedCounter[s];

    // Build table Decoding table
    {
        int i;
        for (i=0; i<tableSize; i++)
        {
            BYTE s = tableDecode[i].symbol;
            U32 nextState = symbolNext[s]++;
            tableDecode[i].nbBits = (BYTE)(tableLog - FSE_highbit(nextState));
            tableDecode[i].newState = (U16)((nextState << tableDecode[i].nbBits) - tableSize);
        }
    }

#if FSE_DEBUG
    {
        int s, total=0;

        // Check count
        for (s=0; s<nbSymbols; s++) total += normalizedCounter[s];
        if (total != tableSize) printf("Count issue ! %i != %i \n", total, tableSize);

        // Check table
        for (s=0; s<nbSymbols; s++)
        {
            int nb=0, i;
            for (i=0; i<tableSize; i++) if (tableDecode[i].symbol==s) nb++;
            if ((U32)nb != normalizedCounter[s]) 
                printf("Count pb for symbol %i : %i present (%i should be)\n", s, nb, normalizedCounter[s]);
        }
    }
#endif

    return 0;
}


int FSE_decodeRaw (void* out, int osize, const BYTE* in)
{
    memcpy(out, in+1, osize);
    return osize+1;
}

int FSE_decodeSingleSymbol (void* out, int osize, const BYTE symbol)
{
    memset(out, symbol, osize);
    return 2;
}


int FSE_decompress_usingDTable(void* dest, const int originalSize, const void* compressed, const void* DTable, const int tableLog)
{
    const BYTE* ip = (const BYTE*) compressed;
    const BYTE* iend;
    BYTE* op = (BYTE*)dest;
    BYTE* const oend = op + originalSize - 1;
    const FSE_decode_t* const decodeTable = (const FSE_decode_t*)DTable;
    U32 bitStream;
    int bitCount;
    U32 state;

    // Init
    bitCount = (((*(U32*)ip)-1) & 7) + 1 + 24;
    iend = ip + (((*(U32*)ip)+7) / 8);
    ip = iend - 4;
    bitStream = *(U32*)ip;

    bitCount = 32-bitCount;
    state = (bitStream << bitCount) >> (32-tableLog);
    bitCount += tableLog;

#if FSE_MAX_TABLELOG > 12
    ip -= bitCount >> 3; bitStream = *(U32*)ip; bitCount &= 7;   // refill bitStream
#endif

    // Hot loop
    while (op<oend)
    {
        U32 rest;
        const int nbBits = decodeTable[state].nbBits;

        *op++ = decodeTable[state].symbol;

        rest = ((bitStream << bitCount) >> 1) >> (31 - nbBits);   // faster than mask
        bitCount += nbBits;

        ip -= bitCount >> 3; bitStream = *(U32*)ip; bitCount &= 7;

        state = decodeTable[state].newState + rest;

#if FSE_DEBUG
        {
            // Check table
            if ((state >= FSE_MAX_TABLESIZE) || (state < 0))
                printf("Error : wrong state %i at pos %i (%i bits read : %x)\n", state, op-(BYTE*)dest, nbBits, rest);
        }
#endif
    }

    // cheap last symbol storage
    *oend = (BYTE)state;

#if FSE_DEBUG
    {
        nbDBlocks ++;
        printf("Decoded block %i \n", nbDBlocks);
    }
#endif

    return (int)(iend-(const BYTE*)compressed);
}


int FSE_decompress (void* dest, int originalSize,
                    const void* compressed)
{
    const BYTE* const istart = (const BYTE*) compressed;
    const BYTE* ip = istart;
    U32   counting[FSE_MAX_NB_SYMBOLS];
    FSE_decode_t DTable[FSE_MAX_TABLESIZE];
    BYTE  headerId;
    int nbSymbols;
    int tableLog;

    // headerId early outs
    headerId = ip[0] & 3;
    if (headerId==0) return FSE_decodeRaw(dest, originalSize, istart);
    if (headerId==1) return FSE_decodeSingleSymbol(dest, originalSize, istart[1]);

    // normal FSE decoding mode
    ip += FSE_readHeader(counting, &nbSymbols, &tableLog, istart);
    FSE_buildDTable(DTable, counting, nbSymbols, tableLog);
    ip += FSE_decompress_usingDTable(dest, originalSize, ip, DTable, tableLog);

    return (int)(ip-istart);
}
