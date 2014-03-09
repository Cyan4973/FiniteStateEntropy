/* ******************************************************************
   Renorm : High Precision normalization
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



/****************************************************************
   Constants
****************************************************************/
#define FSE_MAX_NB_SYMBOLS 286
#define FSE_MAX_TABLELOG    12
#define FSE_MIN_TABLELOG     5


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
int FSE_highbit (register U32 val)
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


/****************************************************************
   Renormalization
****************************************************************/
#include "logDiffCost.h"

void FSE_sortLogDiff(U32* next, U32* head, U64* cost, U32* realCount, U32* normalizedCount, int nbSymbols)
{
    int s;
    int smallest=0;

    for (s=0; s<nbSymbols; s++)
    {
        if (normalizedCount[s]<=1) cost[s] = 1ULL<<62;
        else cost[s] = realCount[s] * logDiffCost[normalizedCount[s]];
    }

    for(s=1; s<nbSymbols; s++)
    {
        if (cost[s] <= cost[smallest])
        {
            next[s]=smallest;
            smallest=s;
            continue;
        }
        {
            int previous = smallest;
            int current = next[smallest];
            int rank=1;
            while ((rank<s) && (cost[s] > cost[current]))
            {
                rank++;
                previous = current;
                current = next[current];
            }

            if (rank==s)
            {
                next[previous]=s;
            }
            else
            {
                next[previous] = s;
                next[s] = current;
            }
        }
    }
    *head = smallest;
}


void FSE_updateSort(U32* head, U32* next, U64* cost, int nbSymbols)
{
    const U64 newCost = cost[*head];
    U32 currentId = next[*head];
    U32 previousId, nextHead;
    int rank;

    if (newCost < cost[currentId]) return;

    nextHead = previousId = currentId;
    currentId = next[currentId];
    rank=2;

    while ((rank<nbSymbols) && (cost[currentId] < newCost)) { previousId = currentId; currentId = next[currentId]; rank++; }
    next[previousId] = *head;
    next[*head] = currentId;
    *head = nextHead;
}


// For explanations on how it works, see : http://fastcompression.blogspot.fr/2014/03/perfect-normalization.html
int FSE_normalizeCountHC (unsigned int* normalizedCounter, int tableLog, unsigned int* count, int total, int nbSymbols)
{
    // Checks
    if (tableLog==0) tableLog = FSE_MAX_TABLELOG;
    if ((FSE_highbit(total-1)+1) < tableLog) tableLog = FSE_highbit(total-1)+1;   // Useless accuracy
    if ((FSE_highbit(nbSymbols)+1) > tableLog) tableLog = FSE_highbit(nbSymbols-1)+1;   // Need a minimum to represent all symbol values
    if (tableLog < FSE_MIN_TABLELOG) tableLog = FSE_MIN_TABLELOG;
    if (tableLog > FSE_MAX_TABLELOG) return -1;   // Unsupported size

    {
        U64 const scale = 62 - tableLog;
        U64 const vStep = (U64)1 << scale;
        U64 const step = ((U64)1<<62) / total;   // <== (lone) division detected...
        U32 realCount[FSE_MAX_NB_SYMBOLS];
        U32 next[FSE_MAX_NB_SYMBOLS];
        U64 cost[FSE_MAX_NB_SYMBOLS];
        U32 smallest;
        int attributed = 0;
        int s;

        // save to realCount, in case count == normalizedCount
        for (s=0; s<nbSymbols; s++) realCount[s] = count[s];

        for (s=0; s<nbSymbols; s++)
        {
            if (count[s]== (U32) total) return 0;   // There is only one symbol
            attributed += normalizedCounter[s] = (U32)(((realCount[s]*step) + (vStep-1)) >> scale);   // Round up
        }

        FSE_sortLogDiff(next, &smallest, cost, realCount, normalizedCounter, nbSymbols);

        while (attributed > (1<<tableLog))
        {
            //printf("symbol : %3i : cost %6.1f bits  -  count %6i : %.2f  (from %i -> %i)\n", smallest, (double)cost[smallest] / logDiffCost[2], realCount[smallest], (double)realCount[smallest] / total * (1<<tableLog), normalizedCounter[smallest], normalizedCounter[smallest]-1);
            normalizedCounter[smallest]--;
            attributed--;
            cost[smallest] = realCount[smallest] * logDiffCost[normalizedCounter[smallest]];
            FSE_updateSort(&smallest, next, cost, nbSymbols);
        }
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



