/* ******************************************************************
   FSEU16 : Finite State Entropy coder for 16-bits input
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
// Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
#define FSE_MAX_MEMORY_USAGE 14
#define FSE_DEFAULT_MEMORY_USAGE 13

// FSE_ILP :
// Determine if the algorithm tries to explicitly exploit ILP
// (Instruction Level Parallelism)
// Default : Recommended
#define FSE_ILP 1


//****************************************************************
//* Includes
//****************************************************************
#include "fseU16.h"


//****************************************************************
//* Compiler specifics
//****************************************************************
#ifdef _MSC_VER    // Visual Studio
#  pragma warning(disable : 4214)        // disable: C4214: non-int bitfields
#endif


/****************************************************************
  Complex types
****************************************************************/
typedef struct
{
    unsigned short newState;
    unsigned char  nbBits : 4;
    unsigned short symbol : 12;
} FSE_decode_tU16;


/********************************************************************
   Include type-specific functions from fse.c (C template emulation)
********************************************************************/
#define FSE_DONTINCLUDECORE

#define FSE_FUNCTION_TYPE U16
#define FSE_FUNCTION_EXTENSION U16
#include "fse.c"   // FSE_countU16, FSE_buildCTableU16, FSE_buildDTableU16


/*********************************************************
  U16 Compression functions
*********************************************************/

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


int FSE_compressU16 (void* dest, const unsigned short* source, unsigned sourceSize, unsigned maxSymbolValue, unsigned tableLog)
{
    const U16* const istart = source;
    const U16* ip = istart;

    BYTE* const ostart = (BYTE*) dest;
    BYTE* op = ostart;

    U32   counting[FSE_MAX_SYMBOL_VALUE+1];
    S16   norm[FSE_MAX_SYMBOL_VALUE+1];
    CTable_max_t CTable;

    int   errorCode;


    // early out
    if (sourceSize <= 1) return FSE_noCompressionU16 (ostart, istart, sourceSize);
    if (!maxSymbolValue) maxSymbolValue = FSE_MAX_SYMBOL_VALUE;
    if (!tableLog) tableLog = FSE_DEFAULT_TABLELOG;

    // Scan for stats
    errorCode = FSE_countU16 (counting, ip, sourceSize, &maxSymbolValue);
    if (errorCode == -1) return -1;
    if (errorCode==(int)sourceSize) return FSE_writeSingleU16(ostart, *istart);

    // Normalize
    errorCode = FSE_normalizeCount (norm, tableLog, counting, sourceSize, maxSymbolValue);
    if (errorCode == -1) return -1;
    if (errorCode ==  0) return FSE_writeSingleU16(ostart, *istart);
    tableLog = errorCode;

    // Write table description header
    errorCode = FSE_writeHeader (op, FSE_headerBound(maxSymbolValue, tableLog), norm, maxSymbolValue, tableLog);
    if (errorCode == -1) return -1;
    op += errorCode;

    // Compress
    errorCode = FSE_buildCTableU16 (&CTable, norm, maxSymbolValue, tableLog);
    if (errorCode==-1) return -1;
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


int FSE_decompressU16_usingDTable (unsigned short* dest, const int originalSize,
                                   const void* compressed, int maxCompressedSize,
                                   const void* DTable, const int tableLog,
                                   int safe)
{
    const BYTE* ip = (const BYTE*) compressed;
    const BYTE* iend;
    U16* op = dest;
    U16* const oend = op + originalSize - 1;
    bitStream_backward_t bitC;
    U32 state;

    // Init
    iend = ip + ( ( (* (U32*) ip) +7) / 8);
    if (safe && (iend < ip)) return -1;   // Memory overflow
    if (safe && (iend > (const BYTE*)compressed + maxCompressedSize)) return -1;   // Beyond input buffer

    bitC.bitsConsumed = ( ( (* (U32*) ip)-1) & 7) + 1 + 24;
    ip = iend - 4;
    bitC.bitContainer = * (U32*) ip;

    bitC.bitsConsumed = 32 - bitC.bitsConsumed;
    state = (bitC.bitContainer << bitC.bitsConsumed) >> (32-tableLog);
    bitC.bitsConsumed += tableLog;

    FSE_updateBitStream(&bitC, (const void**)&ip);

    // 2 symbols per loop
    while( ((safe) && ((op<oend-1) && (ip>=(const BYTE*)compressed)))
        || ((!safe) && (op<oend-1)) )
    {
        *op++ = FSE_decodeSymbolU16(&state, bitC.bitContainer, &bitC.bitsConsumed, DTable);
        if ((sizeof(U32)*8 > FSE_MAX_TABLELOG*2+7) && (sizeof(void*)==8))   // Need this test to be static
            *op++ = FSE_decodeSymbolU16(&state, bitC.bitContainer, &bitC.bitsConsumed, DTable);
        FSE_updateBitStream(&bitC, (const void**)&ip);
    }
    // last symbol
    if (op<oend) *(oend-1) = FSE_decodeSymbolU16(&state, bitC.bitContainer, &bitC.bitsConsumed, DTable);

    // cheap last symbol storage
    *oend = (U16) state;

    if ((ip!=(const BYTE*)compressed) || bitC.bitsConsumed) return -1;   // Not fully decoded stream

    return (int) (iend- (const BYTE*)compressed);
}


int FSE_decompressU16_generic(
                    unsigned short* dest, int originalSize,
                    const void* compressed, int maxCompressedSize,
                    int safe)
{
    const BYTE* const istart = (const BYTE*) compressed;
    const BYTE* ip = istart;
    short   counting[FSE_MAX_SYMBOL_VALUE+1];
    FSE_decode_tU16 DTable[FSE_MAX_TABLESIZE];
    BYTE  headerId;
    unsigned maxSymbolValue;
    unsigned tableLog;
    int errorCode;

    if ((safe) && (maxCompressedSize<3)) return -1;   // too small input size

    // headerId early outs
    headerId = ip[0] & 3;
    if (ip[0]==0)   // Raw (uncompressed) data
    {
        if (safe && maxCompressedSize < 2*originalSize + 1) return -1;
        return FSE_decompressRawU16 (dest, originalSize, istart);
    }
    if (headerId==1) return FSE_decompressSingleU16 (dest, originalSize, *(U16*)(istart+1));
    if (headerId!=2) return -1;   // unused headerId

    // normal FSE decoding mode
    errorCode = FSE_readHeader (counting, &maxSymbolValue, &tableLog, istart);
    if (errorCode==-1) return -1;
    ip += errorCode;

    errorCode = FSE_buildDTableU16 (DTable, counting, maxSymbolValue, tableLog);
    if (errorCode==-1) return -1;

    if (safe) errorCode = FSE_decompressU16_usingDTable (dest, originalSize, ip, maxCompressedSize, DTable, tableLog, 1);
    else errorCode = FSE_decompressU16_usingDTable (dest, originalSize, ip, 0, DTable, tableLog, 0);
    if (errorCode==-1) return -1;
    ip += errorCode;

    return (int) (ip-istart);
}


int FSE_decompressU16 (unsigned short* dest, unsigned originalSize, const void* compressed)
{ return FSE_decompressU16_generic(dest, originalSize, compressed, 0, 0); }

int FSE_decompressU16_safe (unsigned short* dest, unsigned originalSize, const void* compressed, unsigned maxCompressedSize)
{ return FSE_decompressU16_generic(dest, originalSize, compressed, maxCompressedSize, 1); }

