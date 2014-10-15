/*
    fullbench.c - Demo program to benchmark open-source compression algorithm
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

    You can contact the author at :
    - public forum : https://groups.google.com/forum/#!forum/lz4c
    - website : http://fastcompression.blogspot.com/
*/

//**************************************
// Compiler Specific
//**************************************
// GCC does not support _rotl outside of Windows
#if !defined(_WIN32)
#  define _rotl(x,r) ((x << r) | (x >> (32 - r)))
#endif


//**************************************
// Includes
//**************************************
#include <stdlib.h>      // malloc
#include <stdio.h>       // fprintf, fopen, ftello64
#include <string.h>      // strcmp
#include <sys/timeb.h>   // timeb

#include "fse.h"
#include "fseU16.h"
#include "xxhash.h"


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


//****************************
// Constants
//****************************
#define PROGRAM_DESCRIPTION "FSE speed analyzer"
#ifndef FSE_VERSION
#  define FSE_VERSION ""
#endif
#define AUTHOR "Yann Collet"
#define WELCOME_MESSAGE "*** %s %s %i-bits, by %s (%s) ***\n", PROGRAM_DESCRIPTION, FSE_VERSION, (int)(sizeof(void*)*8), AUTHOR, __DATE__

#define NBLOOPS    6
#define TIMELOOP   2500
#define PROBATABLESIZE 2048

#define KB *(1<<10)
#define MB *(1<<20)
#define GB *(1<<30)

#define PRIME1   2654435761U
#define PRIME2   2246822519U
#define DEFAULT_BLOCKSIZE (64 KB)
#define DEFAULT_PROBA 20

//**************************************
// Local structures
//**************************************


//**************************************
// MACRO
//**************************************
#define DISPLAY(...) fprintf(stderr, __VA_ARGS__)
#define PROGRESS(...) no_prompt ? 0 : DISPLAY(__VA_ARGS__)


//**************************************
// Benchmark Parameters
//**************************************
static int no_prompt = 0;


/*********************************************************
  Private functions
*********************************************************/

static U32 BMK_GetMilliStart(void)
{
    struct timeb tb;
    U32 nCount;
    ftime( &tb );
    nCount = (U32) (((tb.time & 0xFFFFF) * 1000) +  tb.millitm);
    return nCount;
}

static U32 BMK_GetMilliSpan(U32 nTimeStart)
{
    U32 nCurrent = BMK_GetMilliStart();
    U32 nSpan = nCurrent - nTimeStart;
    if (nTimeStart > nCurrent)
        nSpan += 0x100000 * 1000;
    return nSpan;
}

static U32 BMK_rand (U32* seed)
{
    *seed =  ( (*seed) * PRIME1) + PRIME2;
    return (*seed) >> 11;
}

static void BMK_genData(void* buffer, size_t buffSize, double p)
{
    char table[PROBATABLESIZE];
    int remaining = PROBATABLESIZE;
    unsigned pos = 0;
    unsigned s = 0;
    char* op = (char*) buffer;
    char* oend = op + buffSize;
    unsigned seed = 1;
    static unsigned done = 0;

    if (p<0.01) p = 0.005;
    if (p>1.) p = 1.;
    if (!done)
    {
        done = 1;
        DISPLAY("\nGenerating %i KB with P=%.2f%%\n", (int)(buffSize >> 10), p*100);
    }

    // Build Table
    while (remaining)
    {
        unsigned n = (unsigned)(remaining * p);
        unsigned end;
        if (!n) n=1;
        end = pos + n;
        while (pos<end) table[pos++]=(char)s;
        s++;
        remaining -= n;
    }

    // Fill buffer
    while (op<oend)
    {
        const unsigned r = BMK_rand(&seed) & (PROBATABLESIZE-1);
        *op++ = table[r];
    }
}


/*********************************************************
  Benchmark function
*********************************************************/
static int local_trivialCount(void* dst, size_t dstSize, const void* src, size_t srcSize)
{

    U32 count[256] = {0};
    const BYTE* ip = (BYTE*)src;
    const BYTE* const end = ip + srcSize;

    (void)dst; (void)dstSize;
    while (ip<end) count[*ip++]++;
    return (int)count[ip[-1]];
}


static int local_count8(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
#define NBT 8
    U32 count[NBT][256];
    const BYTE* ip = (BYTE*)src;
    const BYTE* const end = ip + srcSize - (NBT-1);

    (void)dst; (void)dstSize;
    memset(count, 0, sizeof(count));
    while (ip<end)
    {
        unsigned idx;
        for (idx=0; idx<NBT; idx++)
            count[idx][*ip++]++;
    }
    {
        unsigned idx, n;
        for (n=0; n<256; n++)
            for (idx=1; idx<NBT; idx++)
                count[0][n] += count[idx][n];
    }
    return (int)count[0][ip[-1]];
}


// U64 version
static int local_count8v2(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    U32 count[8][256+16];
    U64* ptr = (U64*) src;
    U64* end = (U64*) ((BYTE*)src + (srcSize & (~0x7)));
    U64 next = *ptr++;

    (void)dst; (void)dstSize;
    memset(count, 0, sizeof(count));

    while (ptr != end)
    {
        register U64 bs = next;
        next = *ptr++;

        count[ 0][(BYTE)bs] ++;
        count[ 1][(BYTE)(bs>>8)] ++;
        count[ 2][(BYTE)(bs>>16)] ++;
        count[ 3][(BYTE)(bs>>24)] ++;
        count[ 4][(BYTE)(bs>>32)] ++;
        count[ 5][(BYTE)(bs>>40)] ++;
        count[ 6][(BYTE)(bs>>48)] ++;
        count[ 7][(BYTE)(bs>>56)] ++;
    }

    {
        unsigned i;
        for (i = 0; i < 256; i++)
        {
            unsigned idx;
            for (idx=1; idx<8; idx++)
                count[0][i] += count[idx][i];
        }
    }

    return count[0][0];
}


// hist_X_Y function from https://github.com/powturbo/turbohist
static int local_hist_4_32(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
//#define NU 8
  #define NU 16
  int i;
  U32 count[256]={0};
  U32 c0[256]={0},c1[256]={0},c2[256]={0},c3[256]={0};
  const BYTE* ip = (const BYTE*)src;
  const BYTE* const iend = ip + (srcSize&(~(NU-1)));
  U32 cp = *(U32 *)src;

  (void)dst; (void)dstSize;

  for(; ip != iend; )
  {
    U32 c = cp; ip += 4; cp = *(U32 *)ip;
    c0[(unsigned char)c      ]++;
    c1[(unsigned char)(c>>8) ]++;
    c2[(unsigned char)(c>>16)]++;
    c3[c>>24                 ]++;

    	     c = cp; ip += 4; cp = *(unsigned *)ip;
    c0[(unsigned char)c      ]++;
    c1[(unsigned char)(c>>8) ]++;
    c2[(unsigned char)(c>>16)]++;
    c3[c>>24                 ]++;

      #if NU == 16
    	     c = cp; ip += 4; cp = *(unsigned *)ip;
    c0[(unsigned char)c      ]++;
    c1[(unsigned char)(c>>8) ]++;
    c2[(unsigned char)(c>>16)]++;
    c3[c>>24                 ]++;

             c = cp; ip += 4; cp = *(unsigned *)ip;
    c0[(unsigned char)c      ]++;
    c1[(unsigned char)(c>>8) ]++;
    c2[(unsigned char)(c>>16)]++;
    c3[c>>24                 ]++;
      #endif
  }
  while(ip < (const BYTE*)src+srcSize) c0[*ip++]++;
  for(i = 0; i < 256; i++)
    count[i] = c0[i]+c1[i]+c2[i]+c3[i];

  return count[0];
}


static int local_hist_4_32v2(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
  U32 c0[256]={0},c1[256]={0},c2[256]={0},c3[256]={0};
  const BYTE* ip = (BYTE*)src;
  const BYTE* const iend = (const BYTE*)src + srcSize;
  U32 cp = *(U32*)src;
  int i;


  (void)dst; (void)dstSize;

  while (ip <= iend-16)
  {
    U32 c = cp,	d = *(U32*)(ip+=4); cp = *(U32*)(ip+=4);
    c0[(BYTE) c    ]++;
    c1[(BYTE) d    ]++;
    c2[(BYTE)(c>>8)]++; c>>=16;
    c3[(BYTE)(d>>8)]++; d>>=16;
    c0[(BYTE) c    ]++;
    c1[(BYTE) d    ]++;
    c2[ 	  c>>8 ]++;
    c3[ 	  d>>8 ]++;

    c = cp;	d = *(U32*)(ip+=4); cp = *(U32*)(ip+=4);
    c0[(BYTE) c    ]++;
    c1[(BYTE) d    ]++;
    c2[(BYTE)(c>>8)]++; c>>=16;
    c3[(BYTE)(d>>8)]++; d>>=16;
    c0[(BYTE) c    ]++;
    c1[(BYTE) d    ]++;
    c2[ 	  c>>8 ]++;
    c3[ 	  d>>8 ]++;
  }
  while(ip < iend) c0[*ip++]++;

  for(i = 0; i < 256; i++) c0[i] += c1[i]+c2[i]+c3[i];

  return c0[0];
}


#define PAD 8

static int local_hist_8_32(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    U32 c0[256+PAD]={0},c1[256+PAD]={0},c2[256+PAD]={0},c3[256+PAD]={0},c4[256+PAD]={0},c5[256+PAD]={0},c6[256+PAD]={0},c7[256+PAD]={0};
    const BYTE* ip = (BYTE*)src;
    const BYTE* const iend = (const BYTE*)src + srcSize;
    U32 cp = *(U32*)src;
    int i;

    (void)dst; (void)dstSize;

    while( ip <= iend-16 )
    {
        U32 c = cp,	d = *(U32 *)(ip+=4); cp = *(U32 *)(ip+=4);
        c0[(unsigned char) c ]++;
        c1[(unsigned char) d ]++;
        c2[(unsigned char)(c>>8)]++; c>>=16;
        c3[(unsigned char)(d>>8)]++; d>>=16;
        c4[(unsigned char) c ]++;
        c5[(unsigned char) d ]++;
        c6[ c>>8 ]++;
        c7[ d>>8 ]++;
        c = cp;	d = *(unsigned *)(ip+=4); cp = *(unsigned *)(ip+=4);
        c0[(unsigned char) c ]++;
        c1[(unsigned char) d ]++;
        c2[(unsigned char)(c>>8)]++; c>>=16;
        c3[(unsigned char)(d>>8)]++; d>>=16;
        c4[(unsigned char) c ]++;
        c5[(unsigned char) d ]++;
        c6[ c>>8 ]++;
        c7[ d>>8 ]++;
    }

    while(ip < iend) c0[*ip++]++;
    for(i = 0; i < 256; i++) c0[i] += c1[i]+c2[i]+c3[i]+c4[i]+c5[i]+c6[i]+c7[i];

    return c0[0];
}


// Modified version of count2x64 by Nathan Kurz, using C instead of assembler
#define C_INC_TABLES(src0, src1, count, i) \
        { \
            U64 byte0 = src0 & 0xFF;\
            U64 byte1 = src1 & 0xFF;\
            U64 byte2 = (src0 & 0xFF00) >> 8; \
            U64 byte3 = (src1 & 0xFF00) >> 8; \
            count[i+0][byte0]++;\
            count[i+1][byte1]++;\
            count[i+2][byte2]++; \
            count[i+3][byte3]++; \
        }

#define COUNT_SIZE (256+16)
static int local_count2x64v2(void* dst, size_t dstSize, const void* src0, size_t srcSize)
{
    const BYTE* src = (const BYTE*)src0;
    U64 remainder = srcSize;
    U64 next0, next1;

    U32 count[16][COUNT_SIZE];
    const BYTE *endSrc;

   (void)dst; (void)dstSize;
    memset(count, 0, sizeof(count));
    if (srcSize < 32) goto handle_remainder;

    remainder = srcSize % 16;
    srcSize -= (size_t)remainder;
    endSrc = src + srcSize;
    next0 = *(U64 *)(src + 0);
    next1 = *(U64 *)(src + 8);

    while (src != endSrc)
    {
        U64 data0 = next0;
        U64 data1 = next1;

        src += 16;
        next0 = *(U64 *)(src + 0);
        next1 = *(U64 *)(src + 8);

        C_INC_TABLES(data0, data1, count, 0);

        data0 >>= 16;
        data1 >>= 16;
        C_INC_TABLES(data0, data1, count, 0);

        data0 >>= 16;
        data1 >>= 16;
        C_INC_TABLES(data0, data1, count, 0);

        data0 >>= 16;
        data1 >>= 16;
        C_INC_TABLES(data0, data1, count, 0);
    }


handle_remainder:
    {
        size_t i;
        for (i = 0; i < remainder; i++)
        {
            U64 byte = src[i];
            count[0][byte]++;
        }

        for (i = 0; i < 256; i++)
        {
            int idx;
            for (idx=1; idx < 16; idx++)
            {
                count[0][i] += count[idx][i];
            }
        }
    }

    return count[0][0];
}

#ifdef __x86_64__

// test function from Nathan Kurz, at https://github.com/nkurz/countbench
#define ASM_SHIFT_RIGHT(reg, bitsToShift)                                \
    __asm volatile ("shr %1, %0":                                       \
                    "+r" (reg): /* read and written */                  \
                    "i" (bitsToShift) /* constant */                    \
                    )


#define ASM_INC_TABLES(src0, src1, byte0, byte1, offset, size, base, scale) \
    __asm volatile ("movzbl %b2, %k0\n"                /* byte0 = src0 & 0xFF */ \
                    "movzbl %b3, %k1\n"                /* byte1 = src1 & 0xFF */ \
                    "incl (%c4+0)*%c5(%6, %0, %c7)\n"  /* count[i+0][byte0]++ */ \
                    "incl (%c4+1)*%c5(%6, %1, %c7)\n"  /* count[i+1][byte1]++ */ \
                    "movzbl %h2, %k0\n"                /* byte0 = (src0 & 0xFF00) >> 8 */ \
                    "movzbl %h3, %k1\n"                /* byte1 = (src1 & 0xFF00) >> 8 */ \
                    "incl (%c4+2)*%c5(%6, %0, %c7)\n"  /* count[i+2][byte0]++ */ \
                    "incl (%c4+3)*%c5(%6, %1, %c7)\n": /* count[i+3][byte1]++ */ \
                    "=&R" (byte0),  /* write only (R == non REX) */     \
                    "=&R" (byte1):  /* write only (R == non REX) */     \
                    "Q" (src0),  /* read only (Q == must have rH) */    \
                    "Q" (src1),  /* read only (Q == must have rH) */    \
                    "i" (offset), /* constant array offset */           \
                    "i" (size), /* constant array size     */           \
                    "r" (base),  /* read only array address */          \
                    "i" (scale):  /* constant [1,2,4,8] */              \
                    "memory" /* clobbered (forces compiler to compute sum ) */ \
                    )

#define COUNT_SIZE (256+16)
static int local_count2x64(void* dst, size_t dstSize, const void* src0, size_t srcSize)
{
    const BYTE* src = (const BYTE*)src0;
    U64 remainder = srcSize;
    if (srcSize < 32) goto handle_remainder;

    U32 count[16][COUNT_SIZE];
    memset(count, 0, sizeof(count));

   (void)dst; (void)dstSize;

    remainder = srcSize % 16;
    srcSize -= remainder;
    const BYTE *endSrc = src + srcSize;
    U64 next0 = *(U64 *)(src + 0);
    U64 next1 = *(U64 *)(src + 8);

    while (src != endSrc)
    {
        U64 byte0, byte1;
        U64 data0 = next0;
        U64 data1 = next1;

        src += 16;
        next0 = *(U64 *)(src + 0);
        next1 = *(U64 *)(src + 8);

        ASM_INC_TABLES(data0, data1, byte0, byte1, 0, COUNT_SIZE * 4, count, 4);

        ASM_SHIFT_RIGHT(data0, 16);
        ASM_SHIFT_RIGHT(data1, 16);
        ASM_INC_TABLES(data0, data1, byte0, byte1, 4, COUNT_SIZE * 4, count, 4);

        ASM_SHIFT_RIGHT(data0, 16);
        ASM_SHIFT_RIGHT(data1, 16);
        ASM_INC_TABLES(data0, data1, byte0, byte1, 8, COUNT_SIZE * 4, count, 4);

        ASM_SHIFT_RIGHT(data0, 16);
        ASM_SHIFT_RIGHT(data1, 16);
        ASM_INC_TABLES(data0, data1, byte0, byte1, 12, COUNT_SIZE * 4, count, 4);
    }


 handle_remainder:
    for (size_t i = 0; i < remainder; i++) {
        uint64_t byte = src[i];
        count[0][byte]++;
    }

    for (int i = 0; i < 256; i++) {
        for (int idx=1; idx < 16; idx++) {
            count[0][i] += count[idx][i];
        }
    }

    return count[0][0];
}

#endif // __x86_64__


#ifdef __SSE4_1__

//#include <emmintrin.h>
//#include <smmintrin.h>
#include <x86intrin.h>

static int local_countVector(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    // SSE version, suggested by Miklos Maroti
    unsigned int count[256];
    struct data { unsigned int a, b, c, d; } temp[256];
    int i;

    (void)dst; (void)dstSize;

    for (i = 0; i < 256; i += 8)
    {
        temp[i].a = 0;
        temp[i].b = 0;
        temp[i].c = 0;
        temp[i].d = 0;
    }

    __m128i *ptr = (__m128i*) src;
    __m128i *end = (__m128i*) ((BYTE*)src + srcSize);
    while (ptr != end)
    {
        __m128i bs = _mm_load_si128(ptr++);

        temp[_mm_extract_epi8(bs, 0)].a += 1;
        temp[_mm_extract_epi8(bs, 1)].b += 1;
        temp[_mm_extract_epi8(bs, 2)].c += 1;
        temp[_mm_extract_epi8(bs, 3)].d += 1;
        temp[_mm_extract_epi8(bs, 4)].a += 1;
        temp[_mm_extract_epi8(bs, 5)].b += 1;
        temp[_mm_extract_epi8(bs, 6)].c += 1;
        temp[_mm_extract_epi8(bs, 7)].d += 1;
        temp[_mm_extract_epi8(bs, 8)].a += 1;
        temp[_mm_extract_epi8(bs, 9)].b += 1;
        temp[_mm_extract_epi8(bs,10)].c += 1;
        temp[_mm_extract_epi8(bs,11)].d += 1;
        temp[_mm_extract_epi8(bs,12)].a += 1;
        temp[_mm_extract_epi8(bs,13)].b += 1;
        temp[_mm_extract_epi8(bs,14)].c += 1;
        temp[_mm_extract_epi8(bs,15)].d += 1;
    }

    for (i = 0; i < 256; i++)
        count[i] = temp[i].a + temp[i].b + temp[i].c + temp[i].d;

    return count[0];
}


static int local_countVec2(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    // SSE version, based on code by Nathan Kurz; reaches 2010 MB/s
    U32 count[16][256+16];   // +16 to avoid repeated call into the same cache line pool (cache associativity)

    __m128i *ptr = (__m128i*) src;
    __m128i *end = (__m128i*) ((BYTE*)src + (srcSize & (~0xF)));
    __m128i next = _mm_load_si128(ptr++);

    (void)dst; (void)dstSize;
    memset(count, 0, sizeof(count));

    while (ptr != end)
    {
        unsigned i;
        __m128i bs = next;
        next = _mm_load_si128(ptr++);

        for (i=0; i<16; i++)
            count[i][_mm_extract_epi8(bs,i)]++;   // Only compiles in release mode (which unrolls the loop to get constants)
    }

    /* note : unfinished : still requires to count last 16-bytes + %16 rest; okay enough for benchmark */

    {
        unsigned i;
        for (i = 0; i < 256; i++)
        {
            unsigned idx;
            for (idx=1; idx<16; idx++)
                count[0][i] += count[idx][i];
        }
    }

    return count[0][0];
}


// Nathan Kurz vectorial version
#define COUNT_ARRAY_SIZE (256 + 8)
static U32 g_count_0[COUNT_ARRAY_SIZE];
static U32 g_count_1[COUNT_ARRAY_SIZE];
static U32 g_count_2[COUNT_ARRAY_SIZE];
static U32 g_count_3[COUNT_ARRAY_SIZE];
static U32 g_count_4[COUNT_ARRAY_SIZE];
static U32 g_count_5[COUNT_ARRAY_SIZE];
static U32 g_count_6[COUNT_ARRAY_SIZE];
static U32 g_count_7[COUNT_ARRAY_SIZE];
static U32 g_count_8[COUNT_ARRAY_SIZE];
static U32 g_count_9[COUNT_ARRAY_SIZE];
static U32 g_count_A[COUNT_ARRAY_SIZE];
static U32 g_count_B[COUNT_ARRAY_SIZE];
static U32 g_count_C[COUNT_ARRAY_SIZE];
static U32 g_count_D[COUNT_ARRAY_SIZE];
static U32 g_count_E[COUNT_ARRAY_SIZE];
static U32 g_count_F[COUNT_ARRAY_SIZE];

// gcc really wants to use "addl $1, %reg", but somehow 'inc' is faster
#define ASM_INC_ARRAY_INDEX_SCALE(array, index, scale)                  \
    __asm volatile ("incl " #array "(, %0, %c1)" :                      \
                    :    /* no registers written (only memory) */       \
                    "r" (index),                                        \
                    "i" (scale):                                        \
                    "memory" /* clobbers */                             \
                    )

// This function is limited by the number of uops in the loop.  Sustained throughput is
// limited to 4 per cycle, and we use about 100.   If you can figure out how to reduce the number
// of uops in the loop (uops, not instructions) this loop should run faster.
// Perhaps there is some means of using low and high byte (al/ah) registers for addressing?
// Perhaps some way to create two single bytes with a single op?  shrd?  imul?
typedef __m128i xmm_t;
static int local_countVecNate(void* dst, size_t dstSize, const void* voidSrc, size_t srcSize)
{
    (void)dst; (void)dstSize;

    memset(g_count_0, 0, 256 * sizeof(U32));
    memset(g_count_1, 0, 256 * sizeof(U32));
    memset(g_count_2, 0, 256 * sizeof(U32));
    memset(g_count_3, 0, 256 * sizeof(U32));
    memset(g_count_4, 0, 256 * sizeof(U32));
    memset(g_count_5, 0, 256 * sizeof(U32));
    memset(g_count_6, 0, 256 * sizeof(U32));
    memset(g_count_7, 0, 256 * sizeof(U32));
    memset(g_count_8, 0, 256 * sizeof(U32));
    memset(g_count_9, 0, 256 * sizeof(U32));
    memset(g_count_A, 0, 256 * sizeof(U32));
    memset(g_count_B, 0, 256 * sizeof(U32));
    memset(g_count_C, 0, 256 * sizeof(U32));
    memset(g_count_D, 0, 256 * sizeof(U32));
    memset(g_count_E, 0, 256 * sizeof(U32));
    memset(g_count_F, 0, 256 * sizeof(U32));

    const BYTE *src = voidSrc;
    size_t remainder = srcSize % 16;
    srcSize = srcSize - remainder;
    if (srcSize == 0) goto handle_remainder;

    xmm_t nextVec = _mm_loadu_si128((xmm_t *)&src[0]);
    for (size_t i = 16; i < srcSize; i += 16) {
        uint64_t byte;
        xmm_t vec = nextVec;
        nextVec = _mm_loadu_si128((xmm_t *)&src[i]);

        byte = _mm_extract_epi8(vec, 0);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_0, byte, 4);

        byte = _mm_extract_epi8(vec, 1);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_1, byte, 4);

        byte = _mm_extract_epi8(vec, 2);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_2, byte, 4);

        byte = _mm_extract_epi8(vec, 3);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_3, byte, 4);

        byte = _mm_extract_epi8(vec, 4);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_4, byte, 4);

        byte = _mm_extract_epi8(vec, 5);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_5, byte, 4);

        byte = _mm_extract_epi8(vec, 6);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_6, byte, 4);

        byte = _mm_extract_epi8(vec, 7);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_7, byte, 4);

        byte = _mm_extract_epi8(vec, 8);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_8, byte, 4);

        byte = _mm_extract_epi8(vec, 9);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_9, byte, 4);

        byte = _mm_extract_epi8(vec, 10);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_A, byte, 4);

        byte = _mm_extract_epi8(vec, 11);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_B, byte, 4);

        byte = _mm_extract_epi8(vec, 12);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_C, byte, 4);

        byte = _mm_extract_epi8(vec, 13);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_D, byte, 4);

        byte = _mm_extract_epi8(vec, 14);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_E, byte, 4);

        byte = _mm_extract_epi8(vec, 15);
        ASM_INC_ARRAY_INDEX_SCALE(g_count_F, byte, 4);

    }

 handle_remainder:
    src += srcSize;  // skip over the finished part
    for (size_t i = 0; i < remainder; i++) {
        uint64_t byte = src[i];
        ASM_INC_ARRAY_INDEX_SCALE(g_count_0, byte, 4);

    }

    return 0;
}

#endif


static int local_FSE_count255(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    U32 count[256];
    U32 max = 255;
    (void)dst; (void)dstSize;
    return FSE_count(count, (BYTE*)src, (U32)srcSize, &max);
}

static int local_FSE_count254(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    U32 count[256];
    U32 max = 254;
    (void)dst; (void)dstSize;
    return FSE_count(count, (BYTE*)src, (U32)srcSize, &max);
}

extern int FSE_countFast(unsigned* count, const unsigned char* source, unsigned sourceSize, unsigned* maxNbSymbolsPtr);

static int local_FSE_countFast254(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    U32 count[256];
    U32 max = 254;
    (void)dst; (void)dstSize;
    return FSE_countFast(count, (BYTE*)src, (U32)srcSize, &max);
}

static int local_FSE_compress(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)dstSize;
    return FSE_compress(dst, src, (U32)srcSize);
}

static short g_normTable[256];
static U32   g_countTable[256];
static U32   g_tableLog;
static U32   g_CTable[2350];

static int local_FSE_normalizeCount(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)dst; (void)dstSize; (void)src;
    return FSE_normalizeCount(g_normTable, 0, g_countTable, (U32)srcSize, 255);
}

static int local_FSE_writeHeader(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)src; (void)srcSize;
    return FSE_writeHeader(dst, (U32)dstSize, g_normTable, 255, g_tableLog);
}

static int local_FSE_writeHeader_small(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)src; (void)srcSize; (void)dstSize;
    return FSE_writeHeader(dst, 500, g_normTable, 255, g_tableLog);
}

static int local_FSE_buildCTable(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)dst; (void)dstSize; (void)src; (void)srcSize;
    return FSE_buildCTable(g_CTable, g_normTable, 255, g_tableLog);
}

static int local_FSE_compress_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)dstSize;
    return FSE_compress_usingCTable(dst, src, (U32)srcSize, g_CTable);
}



int fullSpeedBench(double proba, U32 nbBenchs, U32 algNb)
{
    size_t benchedSize = DEFAULT_BLOCKSIZE;
    size_t cBuffSize = FSE_compressBound((unsigned)benchedSize);
    void* oBuffer = malloc(benchedSize);
    void* cBuffer = malloc(cBuffSize);
    char* funcName;
    int (*func)(void* dst, size_t dstSize, const void* src, size_t srcSize);


    // Init
    BMK_genData(oBuffer, benchedSize, proba);

    // Bench selection
    switch (algNb)
    {
    case 1:
        funcName = "FSE_count(255)";
        func = local_FSE_count255;
        break;

    case 2:
        funcName = "FSE_count(254)";
        func = local_FSE_count254;
        break;

    case 3:
        funcName = "FSE_countFast(254)";
        func = local_FSE_countFast254;
        break;

    case 4:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, (U32)benchedSize, &max);
            g_tableLog = FSE_optimalTableLog(g_tableLog, (U32)benchedSize, max);
            funcName = "FSE_normalizeCount";
            func = local_FSE_normalizeCount;
            break;
        }

    case 5:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, (U32)benchedSize, &max);
            g_tableLog = FSE_optimalTableLog(g_tableLog, (U32)benchedSize, max);
            FSE_normalizeCount(g_normTable, g_tableLog, g_countTable, (U32)benchedSize, max);
            funcName = "FSE_writeHeader";
            func = local_FSE_writeHeader;
            break;
        }

    case 6:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, (U32)benchedSize, &max);
            g_tableLog = FSE_optimalTableLog(g_tableLog, (U32)benchedSize, max);
            FSE_normalizeCount(g_normTable, g_tableLog, g_countTable, (U32)benchedSize, max);
            funcName = "FSE_writeHeader(small)";
            func = local_FSE_writeHeader_small;
            break;
        }

    case 7:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, (U32)benchedSize, &max);
            g_tableLog = FSE_optimalTableLog(g_tableLog, (U32)benchedSize, max);
            FSE_normalizeCount(g_normTable, g_tableLog, g_countTable, (U32)benchedSize, max);
            funcName = "FSE_buildCTable";
            func = local_FSE_buildCTable;
            break;
        }

    case 8:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, (U32)benchedSize, &max);
            g_tableLog = FSE_normalizeCount(g_normTable, g_tableLog, g_countTable, (U32)benchedSize, max);
            FSE_buildCTable(g_CTable, g_normTable, max, g_tableLog);
            funcName = "FSE_compress_usingCTable";
            func = local_FSE_compress_usingCTable;
            break;
        }

    case 9:
        funcName = "FSE_compress";
        func = local_FSE_compress;
        break;

    /* Specific test functions */
    case 100:
        funcName = "trivialCount";
        func = local_trivialCount;
        break;

    case 101:
        funcName = "count8";
        func = local_count8;
        break;

    case 102:
        funcName = "count8v2";
        func = local_count8v2;
        break;

    case 103:
        funcName = "local_hist_4_32";
        func = local_hist_4_32;
        break;

    case 104:
        funcName = "local_hist_4_32v2";
        func = local_hist_4_32v2;
        break;

    case 105:
        funcName = "local_hist_8_32";
        func = local_hist_8_32;
        break;

    case 106:
        funcName = "local_count2x64v2";
        func = local_count2x64v2;
        break;

#ifdef __x86_64__

    case 150:
        funcName = "local_count2x64";
        func = local_count2x64;
        break;

#endif // __x86_64__


#ifdef __SSE4_1__
    case 200:
        funcName = "local_countVector";
        func = local_countVector;
        break;

    case 201:
        funcName = "local_countVec2";
        func = local_countVec2;
        break;

    case 202:
        funcName = "local_countVecNate";
        func = local_countVecNate;
        break;
#endif

    default:
        DISPLAY("Unknown algorithm number\n");
        exit(-1);
    }

    // Bench
    DISPLAY("\r%79s\r", "");
    {
        double bestTime = 999.;
        U32 benchNb=1;
        int errorCode = 0;
        DISPLAY("%1u-%-24.24s : \r", benchNb, funcName);
        for (benchNb=1; benchNb <= nbBenchs; benchNb++)
        {
            U32 milliTime;
            double averageTime;
            U32 loopNb=0;

            milliTime = BMK_GetMilliStart();
            while(BMK_GetMilliStart() == milliTime);
            milliTime = BMK_GetMilliStart();
            while(BMK_GetMilliSpan(milliTime) < TIMELOOP)
            {
                errorCode = func(cBuffer, cBuffSize, oBuffer, benchedSize);
                if (errorCode < 0) { DISPLAY("Error %s \n", funcName); exit(-1); }
                loopNb++;
            }
            milliTime = BMK_GetMilliSpan(milliTime);
            averageTime = (double)milliTime / loopNb;
            if (averageTime < bestTime) bestTime = averageTime;
            DISPLAY("%1u-%-24.24s : %8.1f MB/s\r", benchNb+1, funcName, (double)benchedSize / bestTime / 1000.);
        }
        DISPLAY("%1u#%-24.24s : %8.1f MB/s   (%i)\n", algNb, funcName, (double)benchedSize / bestTime / 1000., (int)errorCode);
    }

    free(oBuffer);
    free(cBuffer);

    return 0;
}


int usage(char* exename)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [arg] \n", exename);
    DISPLAY( "Arguments :\n");
    DISPLAY( " -b#    : select function to benchmark (default : 0 ==  all)\n");
    DISPLAY( " -H/-h  : Help (this text + advanced options)\n");
    return 0;
}

int usage_advanced(char* exename)
{
    usage(exename);
    DISPLAY( "\nAdvanced options :\n");
    DISPLAY( " -i#    : iteration loops [1-9] (default : %i)\n", NBLOOPS);
    DISPLAY( " -P#    : probability curve, in %% (default : %i%%)\n", DEFAULT_PROBA);
    return 0;
}

int badusage(char* exename)
{
    DISPLAY("Wrong parameters\n");
    usage(exename);
    return 1;
}

int main(int argc, char** argv)
{
    char* exename=argv[0];
    U32 proba = DEFAULT_PROBA;
    U32 nbLoops = NBLOOPS;
    U32 pause = 0;
    U32 algNb = 0;
    int i;
    int result;

    // Welcome message
    DISPLAY(WELCOME_MESSAGE);
    if (argc<1) return badusage(exename);

    for(i=1; i<argc; i++)
    {
        char* argument = argv[i];

        if(!argument) continue;   // Protection if argument empty
        if (!strcmp(argument, "--no-prompt")) { no_prompt = 1; continue; }

        // Decode command (note : aggregated commands are allowed)
        if (*argument=='-')
        {
            argument ++;
            while (*argument!=0)
            {

                switch(*argument)
                {
                case '-':   // valid separator
                    argument++;
                    break;

                    // Display help on usage
                case 'h' :
                case 'H': return usage_advanced(exename);

                    // Select Algo nb
                case 'b':
                    argument++;
                    algNb=0;
                    while ((*argument >='0') && (*argument <='9')) algNb*=10, algNb += *argument++ - '0';
                    break;

                    // Modify Nb loops
                case 'i':
                    argument++;
                    nbLoops=0;
                    while ((*argument >='0') && (*argument <='9')) nbLoops*=10, nbLoops += *argument++ - '0';
                    break;

                    // Modify data probability
                case 'P':
                    argument++;
                    proba=0;
                    while ((*argument >='0') && (*argument <='9')) proba*=10, proba += *argument++ - '0';
                    break;

                    // Pause at the end (hidden option)
                case 'p':
                    pause=1;
                    argument++;
                    break;

                    // Unknown command
                default : return badusage(exename);
                }
            }
            continue;
        }

    }

    if (algNb==0)
    {
        for (i=1; i<=9; i++)
            result = fullSpeedBench((double)proba / 100, nbLoops, i);
    }
    else
        result = fullSpeedBench((double)proba / 100, nbLoops, algNb);

    if (pause) { DISPLAY("press enter...\n"); getchar(); }

    return result;
}

