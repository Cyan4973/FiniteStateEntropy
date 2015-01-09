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
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
   - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

/****************************************************************
*  Tuning parameters
*****************************************************************/
/* MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#define FSE_MAX_MEMORY_USAGE 14
#define FSE_DEFAULT_MEMORY_USAGE 13

/* FSE_ILP :
*  Determine if the algorithm tries to explicitly exploit ILP
*  (Instruction Level Parallelism)
*  Default : Recommended */
#define FSE_ILP 1


/****************************************************************
*  Includes
*****************************************************************/
#include "fseU16.h"


/****************************************************************
*  Compiler specifics
*****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  pragma warning(disable : 4214)        /* disable: C4214: non-int bitfields */
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
*  Include type-specific functions from fse.c (C template emulation)
********************************************************************/
#define FSE_COMMONDEFS_ONLY

#define FSE_FUNCTION_TYPE U16
#define FSE_FUNCTION_EXTENSION U16
#include "fse.c"   /* FSE_countU16, FSE_buildCTableU16, FSE_buildDTableU16 */


/*********************************************************
*  U16 Compression functions
*********************************************************/

static int FSE_compressRleU16(void* dst, U16 value)
{
    *(U16*)dst = value;
    return 2;
}


void FSE_encodeU16(FSE_CStream_t* bitC, FSE_CState_t* statePtr, U16 symbol)
{
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) statePtr->symbolTT;
    const U16* const stateTable = (const U16*) statePtr->stateTable;
    int nbBitsOut  = symbolTT[symbol].minBitsOut;
    nbBitsOut -= (int)((symbolTT[symbol].maxState - statePtr->value) >> 31);
    FSE_addBits(bitC, statePtr->value, nbBitsOut);
    statePtr->value = stateTable[ (statePtr->value >> nbBitsOut) + symbolTT[symbol].deltaFindState];
}


size_t FSE_compressU16_usingCTable (void* dst, size_t maxDstSize,
                              const U16*  src, size_t srcSize,
                              const void* CTable)
{
    const U16* const istart = src;
    const U16* ip;
    const U16* const iend = istart + srcSize;

    BYTE* op = (BYTE*) dst;
    FSE_CStream_t bitC;
    FSE_CState_t CState;


    /* init */
    (void)(maxDstSize);   /* tbd */
    FSE_initCStream(&bitC, op);
    FSE_initCState(&CState, CTable);

    ip=iend;

    /* join to even */
    if (srcSize & 1)
    {
        FSE_encodeU16(&bitC, &CState, *--ip);
        FSE_flushBits(&bitC);
    }

    /* join to mod 4 */
    if (srcSize & 2)
    {
        FSE_encodeU16(&bitC, &CState, *--ip);
        FSE_encodeU16(&bitC, &CState, *--ip);
        FSE_flushBits(&bitC);
    }

    /* 2 or 4 encoding per loop */
    while (ip>istart)
    {
        FSE_encodeU16(&bitC, &CState, *--ip);

        if (sizeof(size_t)*8 < FSE_MAX_TABLELOG*2+7 )   // This test must be static
            FSE_flushBits(&bitC);

        FSE_encodeU16(&bitC, &CState, *--ip);

        if (sizeof(size_t)*8 > FSE_MAX_TABLELOG*4+7 )   // This test must be static
        {
            FSE_encodeU16(&bitC, &CState, *--ip);
            FSE_encodeU16(&bitC, &CState, *--ip);
        }

        FSE_flushBits(&bitC);
    }

    FSE_flushCState(&bitC, &CState);
    return FSE_closeCStream(&bitC);
}


size_t FSE_compressU16(void* dst, size_t maxDstSize,
       const unsigned short* src, size_t srcSize,
       unsigned maxSymbolValue, unsigned tableLog)
{
    const U16* const istart = src;
    const U16* ip = istart;

    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const omax = ostart + maxDstSize;

    U32   counting[FSE_MAX_SYMBOL_VALUE+1];
    S16   norm[FSE_MAX_SYMBOL_VALUE+1];
    CTable_max_t CTable;

    size_t   errorCode;


    /* early out */
    if (srcSize <= 1) return srcSize;
    if (!maxSymbolValue) maxSymbolValue = FSE_MAX_SYMBOL_VALUE;
    if (!tableLog) tableLog = FSE_DEFAULT_TABLELOG;
    if (maxSymbolValue > FSE_MAX_SYMBOL_VALUE) return (size_t)-FSE_ERROR_maxSymbolValue_tooLarge;
    if (tableLog > FSE_MAX_TABLELOG) return (size_t)-FSE_ERROR_tableLog_tooLarge;

    /* Scan for stats */
    errorCode = FSE_countU16 (counting, ip, srcSize, &maxSymbolValue);
    if (FSE_isError(errorCode)) return errorCode;
    if (errorCode == srcSize) return FSE_compressRleU16(ostart, *istart);

    /* Normalize */
    tableLog = FSE_optimalTableLog(tableLog, srcSize, maxSymbolValue);
    errorCode = FSE_normalizeCount (norm, tableLog, counting, srcSize, maxSymbolValue);
    if (FSE_isError(errorCode)) return errorCode;

    /* Write table description header */
    errorCode = FSE_writeHeader (op, FSE_MAX_HEADERSIZE, norm, maxSymbolValue, tableLog);
    if (FSE_isError(errorCode)) return errorCode;
    op += errorCode;

    /* Compress */
    errorCode = FSE_buildCTableU16 (&CTable, norm, maxSymbolValue, tableLog);
    if (FSE_isError(errorCode)) return errorCode;
    op += FSE_compressU16_usingCTable (op, omax - op, ip, srcSize, &CTable);

    /* check compressibility */
    if ( (size_t)(op-ostart) >= (size_t)(srcSize-1)*(sizeof(U16)) )
        return 0;   /* no compression */

    return op-ostart;
}


/*********************************************************
*  U16 Decompression functions
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

U16 FSE_decodeSymbolU16(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD)
{
    const FSE_decode_tU16 DInfo = ((const FSE_decode_tU16*)(DStatePtr->table))[DStatePtr->state];
    U16 symbol;
    bitD_t lowBits;
    const U32 nbBits = DInfo.nbBits;

    symbol = DInfo.symbol;
    lowBits = FSE_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.newState + lowBits;

    return symbol;
}


size_t FSE_decompressU16_usingDTable (U16* dst, size_t maxDstSize,
                               const void* cSrc, size_t cSrcSize,
                               const void* DTable)
{
    U16* const ostart = dst;
    U16* op = ostart;
    U16* const oend = ostart + maxDstSize;
    FSE_DStream_t bitD;
    FSE_DState_t state;

    /* Init */
    FSE_initDStream(&bitD, cSrc, cSrcSize);
    FSE_initDState(&state, &bitD, DTable);

    while((FSE_reloadDStream(&bitD) < 2) && (op<oend))
    {
        *op++ = FSE_decodeSymbolU16(&state, &bitD);
    }

    if (!FSE_endOfDStream(&bitD)) return (size_t)-FSE_ERROR_GENERIC;

    return op-ostart;
}


size_t FSE_decompressU16(U16* dst, size_t maxDstSize,
                  const void* cSrc, size_t cSrcSize)
{
    const BYTE* const istart = (const BYTE*) cSrc;
    const BYTE* ip = istart;
    short   counting[FSE_MAX_SYMBOL_VALUE+1];
    FSE_decode_tU16 DTable[FSE_MAX_TABLESIZE];
    unsigned maxSymbolValue = FSE_MAX_SYMBOL_VALUE;
    unsigned tableLog;
    size_t errorCode;

    /* Sanity check */
    if (cSrcSize<2) return (size_t)-FSE_ERROR_srcSize_wrong;   /* specific corner cases (uncompressed & rle) */

    /* normal FSE decoding mode */
    errorCode = FSE_readHeader (counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
    if (FSE_isError(errorCode)) return (size_t)-FSE_ERROR_GENERIC;
    ip += errorCode;
    cSrcSize -= errorCode;

    errorCode = FSE_buildDTableU16 (DTable, counting, maxSymbolValue, tableLog);
    if (FSE_isError(errorCode)) return (size_t)-FSE_ERROR_GENERIC;

    return FSE_decompressU16_usingDTable (dst, maxDstSize, ip, cSrcSize, DTable);
}
