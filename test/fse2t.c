/*
fse2t.c
low memory FSE coder using Escape Strategy
Copyright (C) Yann Collet 2012-2014
GPL v2 License

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


//****************************************************************
// Tuning parameters
//****************************************************************
// MEMORY_USAGE :
// Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
// Increasing memory usage improves compression ratio
// Reduced memory usage can improve speed, due to cache effect
// Default value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
#define FSE2T_MEMORY_USAGE 14


//**************************************
// Includes
//**************************************
#include <stdlib.h>   // malloc
#include "fse.h"


//**************************************
// Basic Types
//**************************************
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   // C99
# include <stdint.h>
typedef uint8_t  BYTE;
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


//****************************************************************
//* Constants
//****************************************************************
#define FSE2T_MAX_TABLELOG  (FSE2T_MEMORY_USAGE-3)
#define FSE2T_MAX_TABLESIZE (1U<<FSE2T_MAX_TABLELOG)
#define FSE2T_MIN_TABLELOG 5   // == FSE_MIN_TABLELOG (required for generic header)


//**************************************
// Compression
//**************************************
int FSE2T_compress_usingCTable (void* dest, const unsigned char* source, int sourceSize, const void* CTable, const BYTE* translate, const void* escapeCTable, BYTE escapeSymbol)
{
    const BYTE* const istart = (const BYTE*) source;
    const BYTE* ip;
    const BYTE* const iend = istart + sourceSize;

    BYTE* op = (BYTE*) dest;
    U32* streamSizePtr;
    ptrdiff_t state;
    ptrdiff_t state2, state3;
    bitContainer_forward_t bitC = {0,0};
    const void* stateTable;
    const void* symbolTT;
    const void* escapeStateTable;
    const void* escapeSymbolTT;


    streamSizePtr = (U32*)FSE_initCompressionStream((void**)&op, &state, &symbolTT, &stateTable, CTable);
    op-=4;
    streamSizePtr = (U32*)FSE_initCompressionStream((void**)&op, &state, &escapeSymbolTT, &escapeStateTable, escapeCTable);
    state3 = state2 = state;

    ip=iend-1;
    state += *ip--;   // cheap last-symbol storage (assumption : nbSymbols <= 1<<tableLog)

    while (ip>=istart)   // simpler version, one symbol at a time
    {
        {
            BYTE symbol = translate[*ip];
            if (symbol==escapeSymbol)
                FSE_encodeByte(&state, &bitC, *ip, escapeSymbolTT, escapeStateTable);
            FSE_encodeByte(&state2, &bitC, symbol, symbolTT, stateTable);
            ip--;
        }
        {
            BYTE symbol = translate[*ip];
            if (symbol==escapeSymbol)
                FSE_encodeByte(&state, &bitC, *ip, escapeSymbolTT, escapeStateTable);
            FSE_encodeByte(&state3, &bitC, symbol, symbolTT, stateTable);
            ip--;
        }
        FSE_flushBits((void**)&op, &bitC);
    }

    return FSE_closeCompressionStream(op, &bitC, 2, state,state2,0,0, streamSizePtr, CTable);
}


BYTE FSE2T_escapeFind(BYTE* encodedByte, U32* escapeCount, U32* count, int srcSize, int nbSymbols)
{
    U32  min = (srcSize * 15) >> 11;   //  <= x 1.75 average
    BYTE escapeSymbol=255;
    U32  escapeTotal=0;
    int  i;

    // select escape Symbol
    for (i=0; i<nbSymbols; i++)
    {
        if (count[i]<=min)
        {
            escapeSymbol = (BYTE)i;
            break;
        }
    }

    // elagate main table
    for (i=0; i<nbSymbols; i++)
    {
        if (count[i]>min) { encodedByte[i] = (BYTE)i; escapeCount[i] = 0; }
        else { encodedByte[i] = escapeSymbol; escapeCount[i] = count[i]; escapeTotal += count[i]; count[i] = 0; }
    }
    count[escapeSymbol] = escapeTotal;

    return escapeSymbol;
}


int FSE_noCompression (BYTE* out, const BYTE* in, int isize);
int FSE_writeSingleChar (BYTE *out, BYTE symbol);


typedef struct
{
    U16 tableLog;
    U16 nbSymbols;
    U16 stateTable[FSE2T_MAX_TABLESIZE];
    U64 symbolTT[256];
} CTable_max_t;

int FSE2T_compress2(void* dst, const unsigned char* src, int srcSize, int tableLog)
{
    U32 count[256];
    BYTE encodedByte[256];
    BYTE* op = (BYTE*)dst;
    int nbSymbols;
    U32 escapeCount[256];
    BYTE escapeSymbol = 255;
    int escapeTotal = 0;
    CTable_max_t CTable;
    CTable_max_t escapeCTable;
    int errorCode;

    // init
    if (srcSize <= 1) return FSE_noCompression (op, src, srcSize);
    if (tableLog==0) tableLog = FSE2T_MAX_TABLELOG+1;
    tableLog--;   // 2 tables
    if (tableLog > FSE2T_MAX_TABLELOG) tableLog = FSE2T_MAX_TABLELOG;
    if (tableLog < FSE2T_MIN_TABLELOG) tableLog = FSE2T_MIN_TABLELOG;

    // Scan input
    errorCode = FSE_count (count, src, srcSize, 256);
    if (errorCode==-1) return -1;
    if (errorCode==1) return FSE_writeSingleChar (op, *src);   // Only 0 is present
    nbSymbols = errorCode;

    // Elagate main table
    escapeSymbol = FSE2T_escapeFind(encodedByte, escapeCount, count, srcSize, nbSymbols);
    tableLog = FSE_normalizeCount(count, tableLog, count, srcSize, nbSymbols);
    FSE_buildCTable (&CTable, count, nbSymbols, tableLog);

    // Build escape table
    tableLog = FSE_normalizeCount(escapeCount, tableLog, escapeCount, escapeTotal, nbSymbols);
    FSE_buildCTable (&escapeCTable, escapeCount, nbSymbols, tableLog);

    // Write headers
    op += FSE_writeHeader (op, count, nbSymbols, tableLog);
    *op++ = escapeSymbol;
    op += FSE_writeHeader (op, escapeCount, nbSymbols, tableLog);
    // Compress
    op += FSE2T_compress_usingCTable (op, src, srcSize, &CTable, encodedByte, &escapeCTable, escapeSymbol);

    return (int)(op-(BYTE*)dst);
}


int FSE2T_compress(void* dst, const unsigned char* src, int srcSize)
{
    return FSE2T_compress2(dst, src, srcSize, FSE2T_MAX_TABLELOG);
}



//**************************************
// Decompression
//**************************************
int FSE2T_decompress_usingDTable(
    unsigned char* dest, const int originalSize, const void* compressed,
    const void* DTable, const int tableLog,
    const void* escapeDTable, BYTE escapeSymbol)
{
    const void* ip = compressed;
    const void* iend;
    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + originalSize - 1;
    bitContainer_backward_t bitC;
    U32 state;
    int nbStates;

    // Init
    iend = FSE_initDecompressionStream(&bitC, &nbStates, &state, &state, &state, &state, &ip, tableLog);
    if (iend==NULL) return -1;

    // Hot loop
    while(op<oend)
    {
        BYTE symbol = FSE_decodeSymbol(&state, &bitC, DTable);
        if (symbol==escapeSymbol) symbol = FSE_decodeSymbol(&state, &bitC, escapeDTable);
        *op++ = symbol;
        FSE_updateBitStream(&bitC, &ip);
    }

    // cheap last symbol storage
    *oend = (BYTE) state;

    if ((ip!=compressed) || bitC.bitsConsumed) return -1;   // Not fully decoded stream

    return FSE_closeDecompressionStream(iend, ip);
}


int FSE_decompressRaw (void* out, int osize, const BYTE* in);
int FSE_decompressSingleSymbol (void* out, int osize, const BYTE symbol);


int FSE2T_decompress (unsigned char* dest, int originalSize, const void* compressed)
{
    const BYTE* const istart = (const BYTE*)compressed;
    const BYTE* ip = istart;
    U32   counting[256];
    U32   DTable[FSE2T_MAX_TABLESIZE];
    U32   escapeCount[256];
    BYTE  escapeSymbol;
    U32   escapeDTable[FSE2T_MAX_TABLESIZE];
    int nbSymbols;
    int tableLog;
    int errorCode;
    int headerId;

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

    escapeSymbol = *ip++;

    errorCode = FSE_readHeader (escapeCount, &nbSymbols, &tableLog, ip);
    if (errorCode==-1) return -1;
    ip += errorCode;
    errorCode = FSE_buildDTable (escapeDTable, escapeCount, nbSymbols, tableLog);
    if (errorCode==-1) return -1;

    errorCode = FSE2T_decompress_usingDTable (dest, originalSize, ip, DTable, tableLog, escapeDTable, escapeSymbol);
    if (errorCode==-1) return -1;
    ip += errorCode;

    return (int) (ip-istart);
}
