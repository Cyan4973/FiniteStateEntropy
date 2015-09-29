/* ******************************************************************
   Huff0 : Huffman coder, part of New Generation Entropy library
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
    - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

/****************************************************************
*  Compiler specifics
****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4214)        /* disable: C4214: non-int bitfields */
#else
#  ifdef __GNUC__
#    define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#    define FORCE_INLINE static inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE static inline
#  endif
#endif


/****************************************************************
*  Includes
****************************************************************/
#include <stdlib.h>     /* malloc, free, qsort */
#include <string.h>     /* memcpy, memset */
#include <stdio.h>      /* printf (debug) */
#include "huff0_static.h"
#include "bitstream.h"
#include "fse.h"        /* header compression */


/****************************************************************
*  Error Management
****************************************************************/
#define HUF_STATIC_ASSERT(c) { enum { HUF_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */


/******************************************
*  FSE helper functions
******************************************/
unsigned HUF_isError(size_t code) { return ERR_isError(code); }

const char* HUF_getErrorName(size_t code) { return ERR_getErrorName(code); }


/*********************************************************
*  Huff0 : Huffman block compression
*********************************************************/
#define HUF_ABSOLUTEMAX_TABLELOG  16   /* absolute limit of HUF_MAX_TABLELOG. Beyond that value, code does not work */
#define HUF_MAX_TABLELOG  12           /* max possible tableLog; for static allocation; can be modified up to HUF_ABSOLUTEMAX_TABLELOG */
#define HUF_DEFAULT_TABLELOG  HUF_MAX_TABLELOG   /* used by default, when not specified */
#define HUF_MAX_SYMBOL_VALUE 255
#if (HUF_MAX_TABLELOG > HUF_ABSOLUTEMAX_TABLELOG)
#  error "HUF_MAX_TABLELOG is too large !"
#endif

typedef struct HUF_CElt_s {
  U16  val;
  BYTE nbBits;
} HUF_CElt ;

typedef struct nodeElt_s {
    U32 count;
    U16 parent;
    BYTE byte;
    BYTE nbBits;
} nodeElt;

/* HUF_writeCTable() :
   return : size of saved CTable */
size_t HUF_writeCTable (void* dst, size_t maxDstSize, const HUF_CElt* tree, U32 maxSymbolValue, U32 huffLog)
{
    BYTE bitsToWeight[HUF_MAX_TABLELOG + 1];
    BYTE huffWeight[HUF_MAX_SYMBOL_VALUE + 1];
    U32 n;
    BYTE* op = (BYTE*)dst;
    size_t size;

     /* check conditions */
    if (maxSymbolValue > HUF_MAX_SYMBOL_VALUE + 1)
        return ERROR(GENERIC);

    /* convert to weight */
    bitsToWeight[0] = 0;
    for (n=1; n<=huffLog; n++)
        bitsToWeight[n] = (BYTE)(huffLog + 1 - n);
    for (n=0; n<maxSymbolValue; n++)
        huffWeight[n] = bitsToWeight[tree[n].nbBits];

    size = FSE_compress(op+1, maxDstSize-1, huffWeight, maxSymbolValue);   /* don't need last symbol stat : implied */
    if (HUF_isError(size)) return size;
    if (size >= 128) return ERROR(GENERIC);   /* should never happen, since maxSymbolValue <= 255 */
    if ((size <= 1) || (size >= maxSymbolValue/2))
    {
        if (size==1)   /* RLE */
        {
            /* only possible case : serie of 1 (because there are at least 2) */
            /* can only be 2^n or (2^n-1), otherwise not an huffman tree */
            BYTE code;
            switch(maxSymbolValue)
            {
            case 1: code = 0; break;
            case 2: code = 1; break;
            case 3: code = 2; break;
            case 4: code = 3; break;
            case 7: code = 4; break;
            case 8: code = 5; break;
            case 15: code = 6; break;
            case 16: code = 7; break;
            case 31: code = 8; break;
            case 32: code = 9; break;
            case 63: code = 10; break;
            case 64: code = 11; break;
            case 127: code = 12; break;
            case 128: code = 13; break;
            default : return ERROR(corruptionDetected);
            }
            op[0] = (BYTE)(255-13 + code);
            return 1;
        }
         /* Not compressible */
        if (maxSymbolValue > (241-128)) return ERROR(GENERIC);   /* not implemented (not possible with current format) */
        if (((maxSymbolValue+1)/2) + 1 > maxDstSize) return ERROR(dstSize_tooSmall);   /* not enough space within dst buffer */
        op[0] = (BYTE)(128 /*special case*/ + 0 /* Not Compressible */ + (maxSymbolValue-1));
		huffWeight[maxSymbolValue] = 0;   /* to be sure it doesn't cause issue in final combination */
        for (n=0; n<maxSymbolValue; n+=2)
            op[(n/2)+1] = (BYTE)((huffWeight[n] << 4) + huffWeight[n+1]);
        return ((maxSymbolValue+1)/2) + 1;
    }

    /* normal header case */
    op[0] = (BYTE)size;
    return size+1;
}


static U32 HUF_setMaxHeight(nodeElt* huffNode, U32 lastNonNull, U32 maxNbBits)
{
    int totalCost = 0;
    const U32 largestBits = huffNode[lastNonNull].nbBits;

    /* early exit : all is fine */
    if (largestBits <= maxNbBits) return largestBits;

    // now we have a few too large elements (at least >= 2)
    {
        const U32 baseCost = 1 << (largestBits - maxNbBits);
        U32 n = lastNonNull;

        while (huffNode[n].nbBits > maxNbBits)
        {
            totalCost += baseCost - (1 << (largestBits - huffNode[n].nbBits));
            huffNode[n].nbBits = (BYTE)maxNbBits;
            n --;
        }

        /* renorm totalCost */
        totalCost >>= (largestBits - maxNbBits);  /* note : totalCost necessarily multiple of baseCost */

        // repay cost
        while (huffNode[n].nbBits == maxNbBits) n--;   // n at last of rank (maxNbBits-1)

        {
            const U32 noOne = 0xF0F0F0F0;
            // Get pos of last (smallest) symbol per rank
            U32 rankLast[HUF_MAX_TABLELOG];
            U32 currentNbBits = maxNbBits;
            int pos;
			memset(rankLast, 0xF0, sizeof(rankLast));
            for (pos=n ; pos >= 0; pos--)
            {
                if (huffNode[pos].nbBits >= currentNbBits) continue;
                currentNbBits = huffNode[pos].nbBits;
                rankLast[maxNbBits-currentNbBits] = pos;
            }

            while (totalCost > 0)
            {
                U32 nBitsToDecrease = BIT_highbit32(totalCost) + 1;
                for ( ; nBitsToDecrease > 1; nBitsToDecrease--)
                {
                    U32 highPos = rankLast[nBitsToDecrease];
                    U32 lowPos = rankLast[nBitsToDecrease-1];
                    if (highPos == noOne) continue;
                    if (lowPos == noOne) break;
                    {
                        U32 highTotal = huffNode[highPos].count;
                        U32 lowTotal = 2 * huffNode[lowPos].count;
                        if (highTotal <= lowTotal) break;
                    }
                }
                while (rankLast[nBitsToDecrease] == noOne)
                    nBitsToDecrease ++;   // In some rare cases, no more rank 1 left => overshoot to closest
                totalCost -= 1 << (nBitsToDecrease-1);
                if (rankLast[nBitsToDecrease-1] == noOne)
                    rankLast[nBitsToDecrease-1] = rankLast[nBitsToDecrease];   // now there is one elt
                huffNode[rankLast[nBitsToDecrease]].nbBits ++;
                if (rankLast[nBitsToDecrease] == 0)
                    rankLast[nBitsToDecrease] = noOne;
                else
                {
                    rankLast[nBitsToDecrease]--;
                    if (huffNode[rankLast[nBitsToDecrease]].nbBits != maxNbBits-nBitsToDecrease)
                        rankLast[nBitsToDecrease] = noOne;   // rank list emptied
                }
            }

			while (totalCost < 0)   /* Sometimes, cost correction overshoot */
			{
                if (rankLast[1] == noOne)   /* special case, no weight 1, let's find it back at n */
                {
                    while (huffNode[n].nbBits == maxNbBits) n--;
                    huffNode[n+1].nbBits--;
                    rankLast[1] = n+1;
                    totalCost++;
                    continue;
                }
                huffNode[ rankLast[1] + 1 ].nbBits--;
                rankLast[1]++;
                totalCost ++;
            }
        }
    }

    return maxNbBits;
}


typedef struct {
    U32 base;
    U32 current;
} rankPos;

static void HUF_sort(nodeElt* huffNode, const U32* count, U32 maxSymbolValue)
{
    rankPos rank[32];
    U32 n;

    memset(rank, 0, sizeof(rank));
    for (n=0; n<=maxSymbolValue; n++)
    {
        U32 r = BIT_highbit32(count[n] + 1);
        rank[r].base ++;
    }
    for (n=30; n>0; n--) rank[n-1].base += rank[n].base;
    for (n=0; n<32; n++) rank[n].current = rank[n].base;
    for (n=0; n<=maxSymbolValue; n++)
    {
        U32 c = count[n];
        U32 r = BIT_highbit32(c+1) + 1;
        U32 pos = rank[r].current++;
        while ((pos > rank[r].base) && (c > huffNode[pos-1].count)) huffNode[pos]=huffNode[pos-1], pos--;
        huffNode[pos].count = c;
        huffNode[pos].byte  = (BYTE)n;
    }
}


#define STARTNODE (HUF_MAX_SYMBOL_VALUE+1)
size_t HUF_buildCTable (HUF_CElt* tree, const U32* count, U32 maxSymbolValue, U32 maxNbBits)
{
    nodeElt huffNode0[2*HUF_MAX_SYMBOL_VALUE+1 +1];
    nodeElt* huffNode = huffNode0 + 1;
    U32 n, nonNullRank;
    int lowS, lowN;
    U16 nodeNb = STARTNODE;
    U32 nodeRoot;

    /* safety checks */
    if (maxNbBits == 0) maxNbBits = HUF_DEFAULT_TABLELOG;
    if (maxSymbolValue > HUF_MAX_SYMBOL_VALUE) return ERROR(GENERIC);
	memset(huffNode0, 0, sizeof(huffNode0));

    // sort, decreasing order
    HUF_sort(huffNode, count, maxSymbolValue);

    // init for parents
    nonNullRank = maxSymbolValue;
    while(huffNode[nonNullRank].count == 0) nonNullRank--;
    lowS = nonNullRank; nodeRoot = nodeNb + lowS - 1; lowN = nodeNb;
    huffNode[nodeNb].count = huffNode[lowS].count + huffNode[lowS-1].count;
    huffNode[lowS].parent = huffNode[lowS-1].parent = nodeNb;
    nodeNb++; lowS-=2;
    for (n=nodeNb; n<=nodeRoot; n++) huffNode[n].count = (U32)(1U<<30);
    huffNode0[0].count = (U32)(1U<<31);

    // create parents
    while (nodeNb <= nodeRoot)
    {
        U32 n1 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
        U32 n2 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
        huffNode[nodeNb].count = huffNode[n1].count + huffNode[n2].count;
        huffNode[n1].parent = huffNode[n2].parent = nodeNb;
        nodeNb++;
    }

    // distribute weights (unlimited tree height)
    huffNode[nodeRoot].nbBits = 0;
    for (n=nodeRoot-1; n>=STARTNODE; n--)
        huffNode[n].nbBits = huffNode[ huffNode[n].parent ].nbBits + 1;
    for (n=0; n<=nonNullRank; n++)
        huffNode[n].nbBits = huffNode[ huffNode[n].parent ].nbBits + 1;

    /* enforce maxTableLog */
    maxNbBits = HUF_setMaxHeight(huffNode, nonNullRank, maxNbBits);

    /* fill result into tree (val, nbBits) */
    {
        U16 nbPerRank[HUF_MAX_TABLELOG+1] = {0};
		U16 valPerRank[HUF_MAX_TABLELOG+1] = {0};
        if (maxNbBits > HUF_MAX_TABLELOG) return ERROR(GENERIC);   /* check fit into table */
        for (n=0; n<=nonNullRank; n++)
            nbPerRank[huffNode[n].nbBits]++;
        {
            /* determine stating value per rank */
            U16 min = 0;
            for (n=maxNbBits; n>0; n--)
            {
                valPerRank[n] = min;      // get starting value within each rank
                min += nbPerRank[n];
                min >>= 1;
            }
        }
        for (n=0; n<=maxSymbolValue; n++)
            tree[huffNode[n].byte].nbBits = huffNode[n].nbBits;   // push nbBits per symbol, symbol order
        for (n=0; n<=maxSymbolValue; n++)
            tree[n].val = valPerRank[tree[n].nbBits]++;   // assign value within rank, symbol order
    }

    return maxNbBits;
}

static void HUF_encodeSymbol(BIT_CStream_t* bitCPtr, U32 symbol, const HUF_CElt* CTable)
{
    BIT_addBitsFast(bitCPtr, CTable[symbol].val, CTable[symbol].nbBits);
}

size_t HUF_compressBound(size_t size) { return HUF_COMPRESSBOUND(size); }

#define HUF_FLUSHBITS(s)  (fast ? BIT_flushBitsFast(s) : BIT_flushBits(s))

#define HUF_FLUSHBITS_1(stream) \
    if (sizeof((stream)->bitContainer)*8 < HUF_MAX_TABLELOG*2+7) HUF_FLUSHBITS(stream)

#define HUF_FLUSHBITS_2(stream) \
    if (sizeof((stream)->bitContainer)*8 < HUF_MAX_TABLELOG*4+7) HUF_FLUSHBITS(stream)

size_t HUF_compress_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable)
{
    const BYTE* ip = (const BYTE*) src;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + dstSize;
    size_t n;
    const unsigned fast = (dstSize >= HUF_BLOCKBOUND(srcSize));
    size_t errorCode;
    BIT_CStream_t bitC;

    /* init */
	if (dstSize < 8) return 0;   /* not enough space to compress */
    errorCode = BIT_initCStream(&bitC, op, oend-op);
    if (HUF_isError(errorCode)) return 0;

    n = srcSize & ~3;  /* join to mod 4 */
    switch (srcSize & 3)
    {
        case 3 : HUF_encodeSymbol(&bitC, ip[n+ 2], CTable);
                 HUF_FLUSHBITS_2(&bitC);
        case 2 : HUF_encodeSymbol(&bitC, ip[n+ 1], CTable);
                 HUF_FLUSHBITS_1(&bitC);
        case 1 : HUF_encodeSymbol(&bitC, ip[n+ 0], CTable);
                 HUF_FLUSHBITS(&bitC);
        case 0 :
        default: ;
    }

    for (; n>0; n-=4)   /* note : n&3==0 at this stage */
    {
        HUF_encodeSymbol(&bitC, ip[n- 1], CTable);
        HUF_FLUSHBITS_1(&bitC);
        HUF_encodeSymbol(&bitC, ip[n- 2], CTable);
        HUF_FLUSHBITS_2(&bitC);
        HUF_encodeSymbol(&bitC, ip[n- 3], CTable);
        HUF_FLUSHBITS_1(&bitC);
        HUF_encodeSymbol(&bitC, ip[n- 4], CTable);
        HUF_FLUSHBITS(&bitC);
    }

    return BIT_closeCStream(&bitC);
}


static size_t HUF_compress_into4Segments(void* dst, size_t dstSize, const void* src, size_t srcSize, const HUF_CElt* CTable)
{
    size_t segmentSize = (srcSize+3)/4;   /* first 3 segments */
    size_t errorCode;
    const BYTE* ip = (const BYTE*) src;
    const BYTE* const iend = ip + srcSize;
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + dstSize;

    if (dstSize < 6 + 1 + 1 + 1 + 8) return 0;   /* minimum space to compress successfully */
    if (srcSize < 12) return 0;   /* no saving possible : too small input */
    op += 6;   /* jumpTable */

    errorCode = HUF_compress_usingCTable(op, oend-op, ip, segmentSize, CTable);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode==0) return 0;
    MEM_writeLE16(ostart, (U16)errorCode);

    ip += segmentSize;
    op += errorCode;
    errorCode = HUF_compress_usingCTable(op, oend-op, ip, segmentSize, CTable);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode==0) return 0;
    MEM_writeLE16(ostart+2, (U16)errorCode);

    ip += segmentSize;
    op += errorCode;
    errorCode = HUF_compress_usingCTable(op, oend-op, ip, segmentSize, CTable);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode==0) return 0;
    MEM_writeLE16(ostart+4, (U16)errorCode);

    ip += segmentSize;
    op += errorCode;
    errorCode = HUF_compress_usingCTable(op, oend-op, ip, iend-ip, CTable);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode==0) return 0;

    op += errorCode;
    return op-ostart;
}


size_t HUF_compress2 (void* dst, size_t dstSize,
                const void* src, size_t srcSize,
                unsigned maxSymbolValue, unsigned huffLog)
{
    BYTE* const ostart = (BYTE*)dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + dstSize;

    U32 count[HUF_MAX_SYMBOL_VALUE+1];
    HUF_CElt CTable[HUF_MAX_SYMBOL_VALUE+1];
    size_t errorCode;

    /* early out */
    if (srcSize <= 1) return srcSize;  /* Uncompressed or RLE */
    if (huffLog > HUF_MAX_TABLELOG) return ERROR(tableLog_tooLarge);
    if (!maxSymbolValue) maxSymbolValue = HUF_MAX_SYMBOL_VALUE;
    if (!huffLog) huffLog = HUF_DEFAULT_TABLELOG;

    /* Scan input and build symbol stats */
    errorCode = FSE_count (count, &maxSymbolValue, (const BYTE*)src, srcSize);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode == srcSize) return 1;
    if (errorCode <= (srcSize >> 7)+1) return 0;   /* Heuristic : not compressible enough */

    /* Build Huffman Tree */
    errorCode = HUF_buildCTable (CTable, count, maxSymbolValue, huffLog);
    if (HUF_isError(errorCode)) return errorCode;
    huffLog = (U32)errorCode;

    /* Write table description header */
    errorCode = HUF_writeCTable (op, dstSize, CTable, maxSymbolValue, huffLog);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode + 12 >= srcSize) return 0;   /* not useful to try compression */
    op += errorCode;

    /* Compress */
    //errorCode = HUF_compress_usingCTable(op, oend - op, src, srcSize, CTable);
    errorCode = HUF_compress_into4Segments(op, oend - op, src, srcSize, CTable);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode==0) return 0;
    op += errorCode;

    /* check compressibility */
    if ((size_t)(op-ostart) >= srcSize-1)
        return op-ostart;

    return op-ostart;
}

size_t HUF_compress (void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    return HUF_compress2(dst, maxDstSize, src, (U32)srcSize, 255, HUF_DEFAULT_TABLELOG);
}


/*********************************************************
*  Huff0 : Huffman block decompression
*********************************************************/
typedef struct { U16 sequence; BYTE nbBits; BYTE length; } HUF_DElt;
//typedef struct { U32 sequence : 24; U32 nbBits : 4; U32 length : 4; } HUF_DElt;

typedef struct {
    BYTE symbol;
    BYTE weight;
} sortedSymbol_t;


static void HUF_fillDTableLevel2(HUF_DElt* DTable, U32 sizeLog, U32 consumed,
                           const U32* rankValOrigin, int minWeight, U32 maxW,
                           const sortedSymbol_t* sortedSymbols, const U32 sortedListSize,
                           U32 nbBitsBaseline, U16 baseSeq)
{
    HUF_DElt DElt;
    U32 rankVal[HUF_MAX_TABLELOG + 1];
    U32 s;

    /* Scale rankVal */
    for (s=minWeight; s<=maxW; s++)   /* note : minWeight >= 1 */
    {
        rankVal[s] = rankValOrigin[s] >> consumed;
    }

    /* fill skipped values */
    if (minWeight>1)
    {
        U32 i, skipSize = rankVal[minWeight];
        MEM_writeLE16(&(DElt.sequence), baseSeq);
        DElt.nbBits   = (BYTE)(consumed);
        DElt.length   = 1;
        for (i = 0; i < skipSize; i++)
            DTable[i] = DElt;
    }

    /* fill DTable */
    for (s=0; s<sortedListSize; s++)
    {
        const U32 symbol = sortedSymbols[s].symbol;
        const U32 weight = sortedSymbols[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 length = 1 << (sizeLog-nbBits);
        const U32 start = rankVal[weight];
        U32 i = start;
        const U32 end = start + length;

        MEM_writeLE16(&(DElt.sequence), (U16)(baseSeq + (symbol << 8)));
        DElt.nbBits   = (BYTE)(nbBits + consumed);
        DElt.length   = 2;
        do { DTable[i++] = DElt; } while (i<end);   /* length >= 1 */

        rankVal[weight] += length;
    }
}


static void HUF_fillDTable(HUF_DElt* DTable, U32 targetLog, U32 srcLog,
                           const sortedSymbol_t* sortedList, const U32 sortedListSize,
                           const U32* rankStart, const U32* rankValOrigin, U32 maxW,
                           const U32 minBits, const U32 nbBitsBaseline)
{
    U32 rankVal[HUF_MAX_TABLELOG + 1];
    int scaleLog = srcLog - targetLog + 1;   /* note : targetLog >= srcLog */
    U32 s;

    memcpy(rankVal, rankValOrigin, sizeof(rankVal));

    /* fill DTable */
    for (s=0; s<sortedListSize; s++)
    {
        const U16 symbol = sortedList[s].symbol;
        const U32 weight = sortedList[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 start = rankVal[weight];
        const U32 length = 1 << (targetLog-nbBits);

        if (targetLog-nbBits >= minBits)   /* enough room for a second symbol */
        {
            int minWeight = nbBits + scaleLog;
            if (minWeight < 1) minWeight = 1;
            HUF_fillDTableLevel2(DTable+start, targetLog-nbBits, nbBits,
                           rankValOrigin, minWeight, maxW,
                           sortedList+rankStart[minWeight], sortedListSize-rankStart[minWeight],
                           nbBitsBaseline, symbol);
        }
        else
        {
            U32 i;
            const U32 end = start + length;
            HUF_DElt DElt;

            MEM_writeLE16(&(DElt.sequence), symbol);
            DElt.nbBits   = (BYTE)(nbBits);
            DElt.length   = 1;
            for (i = start; i < end; i++)
                DTable[i] = DElt;
        }
        rankVal[weight] += length;
    }
}


size_t HUF_readDTable (U32* DTable, const void* src, size_t srcSize)
{
    BYTE weightList[HUF_MAX_SYMBOL_VALUE + 1];
    sortedSymbol_t sortedSymbol[HUF_MAX_SYMBOL_VALUE + 1];
    U32 rankStats[HUF_MAX_TABLELOG + 1] = { 0 };
    U32 rankStart0[HUF_MAX_TABLELOG + 2];
    U32* const rankStart = rankStart0+1;
    U32 rankValOrigin[HUF_MAX_TABLELOG + 1];
    U32 weightTotal;
    U32 tableLog, minBits, maxW, sizeOfSort;
    const BYTE* ip = (const BYTE*) src;
    size_t iSize = ip[0];
    size_t oSize;
    U32 n;
    HUF_DElt* const dt = ((HUF_DElt*)DTable) + 1;

    HUF_STATIC_ASSERT(sizeof(HUF_DElt) == sizeof(U32));   /* if compilation fails here, assertion is false */
    //memset(weightList, 0, sizeof(weightList));   /* is not necessary, although some static analyzer complain ... */

    if (iSize >= 128)  /* special header */
    {
        if (iSize >= (242))   /* RLE - necessarily a trail of 1s */
        {
            static int l[14] = { 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128 };
            oSize = l[iSize-242];
            memset(weightList, 1, HUF_MAX_SYMBOL_VALUE + 1);
            iSize = 0;
        }
        else   /* Incompressible */
        {
            oSize = iSize - 127;
            iSize = ((oSize+1)/2);
            if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
            ip += 1;
            for (n=0; n<oSize; n+=2)
            {
                weightList[n]   = ip[n/2] >> 4;
                weightList[n+1] = ip[n/2] & 15;
            }
        }
    }
    else  /* header compressed with FSE (normal case) */
    {
        if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
        oSize = FSE_decompress(weightList, HUF_MAX_SYMBOL_VALUE, ip+1, iSize);   /* max 255 values decoded, last one is implied */
        if (HUF_isError(oSize)) return oSize;
    }

    /* collect weight stats */
    weightTotal = 0;
    for (n=0; n<oSize; n++)
    {
        if (weightList[n] > HUF_MAX_TABLELOG) return ERROR(corruptionDetected);
        rankStats[weightList[n]]++;
        weightTotal += (1 << weightList[n]) >> 1;
    }

    /* get last non-null symbol weight (implied, total must be 2^n) */
    tableLog = BIT_highbit32(weightTotal) + 1;
    if (tableLog > DTable[0]) return ERROR(tableLog_tooLarge);   /* DTable memory is too small */
    {
        U32 total = 1 << tableLog;
        U32 rest = total - weightTotal;
        U32 verif = 1 << BIT_highbit32(rest);
        U32 lastWeight = BIT_highbit32(rest) + 1;
        if (verif != rest) return ERROR(corruptionDetected);    /* last value must be a clean power of 2 */
        weightList[oSize] = (BYTE)lastWeight;
        rankStats[lastWeight]++;
    }

    /* check tree construction validity */
    if ((rankStats[1] < 2) || (rankStats[1] & 1)) return ERROR(corruptionDetected);   /* by construction : at least 2 elts of rank 1, must be even */

    /* find maxWeight => minBits */
    for (maxW = tableLog; rankStats[maxW]==0; maxW--) ;
    minBits = (tableLog-maxW) + 1;

    /* Get start index of each weight */
    {
        U32 w, nextRankStart = 0;
        for (w=1; w<=maxW; w++)
        {
            U32 current = nextRankStart;
            nextRankStart += rankStats[w];
            rankStart[w] = current;
        }
        rankStart[0] = nextRankStart;   /* put all 0w symbols at the end of sorted list*/
        rankStart[w] = nextRankStart;
        sizeOfSort = nextRankStart;
    }

    /* sort symbols by weight */
    {
        U32 s;
        for (s=0; s<=oSize; s++)
        {
            U32 w = weightList[s];
            U32 r = rankStart[w]++;
            sortedSymbol[r].symbol = (BYTE)s;
            sortedSymbol[r].weight = (BYTE)w;
        }
        rankStart[0] = 0;   /* forget 0w symbols; now is beginning of weight(1) */
    }

    /* Build rankValOrigin */
    {
        U32 nextRankVal = 0;
        U32 w;
        const int rescale = (HUF_MAX_TABLELOG-tableLog) - 1;   /* targetLog >= srcLog */
        for (w=1; w<=maxW; w++)
        {
            U32 current = nextRankVal;
            nextRankVal += rankStats[w] << (w+rescale);
            rankValOrigin[w] = current;
        }
    }


    HUF_fillDTable(dt, HUF_MAX_TABLELOG, tableLog,
                   sortedSymbol, sizeOfSort,
                   rankStart0, rankValOrigin, maxW,
                   minBits, tableLog+1);

    return iSize+1;
}


static U32 HUF_decodeSymbol(void* op, BIT_DStream_t* DStream, const HUF_DElt* dt, const U32 dtLog)
{
    const size_t val = BIT_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 2);
    BIT_skipBits(DStream, dt[val].nbBits);
    return dt[val].length;
}

static U32 HUF_decodeLastSymbol(void* op, BIT_DStream_t* DStream, const HUF_DElt* dt, const U32 dtLog)
{
    const size_t val = BIT_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 1);
    if (dt[val].length==1) BIT_skipBits(DStream, dt[val].nbBits);
    else
    {
        if (DStream->bitsConsumed < (sizeof(DStream->bitContainer)*8))
        {
            BIT_skipBits(DStream, dt[val].nbBits);
            if (DStream->bitsConsumed > (sizeof(DStream->bitContainer)*8))
                DStream->bitsConsumed = (sizeof(DStream->bitContainer)*8);   /* ugly hack; works only because it's the last symbol. Note : can't easily extract nbBits from just this symbol */
        }
    }
    return 1;
}


#define HUF_DECODE_SYMBOL_0(ptr, DStreamPtr) \
    ptr += HUF_decodeSymbol(ptr, DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOL_1(ptr, DStreamPtr) \
	if (MEM_64bits() || (HUF_MAX_TABLELOG<=12)) \
        ptr += HUF_decodeSymbol(ptr, DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOL_2(ptr, DStreamPtr) \
	if (MEM_64bits()) \
        ptr += HUF_decodeSymbol(ptr, DStreamPtr, dt, dtLog)

static size_t HUF_decodeStream(BYTE* p, BIT_DStream_t* bitDPtr, BYTE* const pEnd, const HUF_DElt* const dt, const U32 dtLog)
{
    BYTE* const pStart = p;

    /* up to 8 symbols at a time */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p < pEnd-7))
    {
        HUF_DECODE_SYMBOL_2(p, bitDPtr);
        HUF_DECODE_SYMBOL_1(p, bitDPtr);
        HUF_DECODE_SYMBOL_2(p, bitDPtr);
        HUF_DECODE_SYMBOL_0(p, bitDPtr);
    }

    /* closer to the end */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p <= pEnd-2))
        HUF_DECODE_SYMBOL_0(p, bitDPtr);

    while (p <= pEnd-2)
        HUF_DECODE_SYMBOL_0(p, bitDPtr);   /* no need to reload : reached the end of DStream */

    if (p < pEnd)
        p += HUF_decodeLastSymbol(p, bitDPtr, dt, dtLog);

    return p-pStart;
}


static size_t HUF_decompress_usingDTable(   /* -11% slower when non static */
          void* dst, size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const U32* DTable)
{
    if (cSrcSize < 10) return ERROR(corruptionDetected);   /* strict minimum : jump table + 1 byte per stream */

    {
        const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;

        const HUF_DElt* const dt = ((const HUF_DElt*)DTable) +1;
        const U32 dtLog = DTable[0];
        size_t errorCode;

        /* Init */
        BIT_DStream_t bitD1;
        BIT_DStream_t bitD2;
        BIT_DStream_t bitD3;
        BIT_DStream_t bitD4;
        const size_t length1 = MEM_readLE16(istart);
        const size_t length2 = MEM_readLE16(istart+2);
        const size_t length3 = MEM_readLE16(istart+4);
        size_t length4;
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        const size_t segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;

        length4 = cSrcSize - (length1 + length2 + length3 + 6);
        if (length4 > cSrcSize) return ERROR(corruptionDetected);   /* overflow */
        errorCode = BIT_initDStream(&bitD1, istart1, length1);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD2, istart2, length2);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD3, istart3, length3);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD4, istart4, length4);
        if (HUF_isError(errorCode)) return errorCode;

        /* 16-32 symbols per loop (4-8 symbols per stream) */
        endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
        for ( ; (endSignal==BIT_DStream_unfinished) && (op4<(oend-7)) ; )
        {
            HUF_DECODE_SYMBOL_2(op1, &bitD1);
            HUF_DECODE_SYMBOL_2(op2, &bitD2);
            HUF_DECODE_SYMBOL_2(op3, &bitD3);
            HUF_DECODE_SYMBOL_2(op4, &bitD4);
            HUF_DECODE_SYMBOL_1(op1, &bitD1);
            HUF_DECODE_SYMBOL_1(op2, &bitD2);
            HUF_DECODE_SYMBOL_1(op3, &bitD3);
            HUF_DECODE_SYMBOL_1(op4, &bitD4);
            HUF_DECODE_SYMBOL_2(op1, &bitD1);
            HUF_DECODE_SYMBOL_2(op2, &bitD2);
            HUF_DECODE_SYMBOL_2(op3, &bitD3);
            HUF_DECODE_SYMBOL_2(op4, &bitD4);
            HUF_DECODE_SYMBOL_0(op1, &bitD1);
            HUF_DECODE_SYMBOL_0(op2, &bitD2);
            HUF_DECODE_SYMBOL_0(op3, &bitD3);
            HUF_DECODE_SYMBOL_0(op4, &bitD4);

            endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
        }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruptionDetected);
        if (op2 > opStart3) return ERROR(corruptionDetected);
        if (op3 > opStart4) return ERROR(corruptionDetected);
        /* note : op4 supposed already verified within main loop */

        /* finish bitStreams one by one */
        op1 += HUF_decodeStream(op1, &bitD1, opStart2, dt, dtLog);
        op2 += HUF_decodeStream(op2, &bitD2, opStart3, dt, dtLog);
        op3 += HUF_decodeStream(op3, &bitD3, opStart4, dt, dtLog);
        op4 += HUF_decodeStream(op4, &bitD4, oend,     dt, dtLog);

        /* check */
        endSignal = BIT_endOfDStream(&bitD1) & BIT_endOfDStream(&bitD2) & BIT_endOfDStream(&bitD3) & BIT_endOfDStream(&bitD4);
        if (!endSignal) return ERROR(corruptionDetected);

        /* decoded size */
        return op4-ostart;
    }
}


size_t HUF_decompress (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLE(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;
    size_t errorCode;

    errorCode = HUF_readDTable (DTable, cSrc, cSrcSize);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode >= cSrcSize) return ERROR(srcSize_wrong);
    ip += errorCode;
    cSrcSize -= errorCode;

    return HUF_decompress_usingDTable (dst, dstSize, ip, cSrcSize, DTable);
}
