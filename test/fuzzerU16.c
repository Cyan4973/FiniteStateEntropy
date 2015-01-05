/*
FuzzerU16.c
Automated test program for FSE U16
Copyright (C) Yann Collet 2013-2015

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
- FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
- Public forum : https://groups.google.com/forum/#!forum/lz4c
*/


/******************************
*  Compiler options
******************************/
#define _CRT_SECURE_NO_WARNINGS   /* remove Visual warning */


/******************************
*  Include
******************************/
#include <stdlib.h>    /* malloc, abs */
#include <stdio.h>     /* printf */
#include <string.h>    /* memset */
#include <sys/timeb.h> /* timeb */
#include "fse.h"       /* FSE_isError */
#include "fseU16.h"
#include "xxhash.h"


/****************************************************************
*  Basic Types
****************************************************************/
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
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


/******************************
*  Constants
******************************/
#define KB *(1<<10)
#define MB *(1<<20)
#define BUFFERSIZE ((1 MB) - 1)
#define FUZ_NB_TESTS  (16 KB)
#define PROBATABLESIZE (4 KB)
#define FUZ_UPDATERATE  200
#define PRIME1   2654435761U
#define PRIME2   2246822519U


/***************************************************
*  Macros
***************************************************/
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static unsigned displayLevel = 2;   // 0 : no display  // 1: errors  // 2 : + result + interaction + warnings ;  // 3 : + progression;  // 4 : + information


/***************************************************
*  local functions
***************************************************/
static int FUZ_GetMilliStart(void)
{
    struct timeb tb;
    int nCount;
    ftime ( &tb );
    nCount = (int) (tb.millitm + (tb.time & 0xfffff) * 1000);
    return nCount;
}


static int FUZ_GetMilliSpan ( int nTimeStart )
{
    int nSpan = FUZ_GetMilliStart() - nTimeStart;
    if ( nSpan < 0 )
        nSpan += 0x100000 * 1000;
    return nSpan;
}


static unsigned FUZ_rand (unsigned* src)
{
    *src =  ( (*src) * PRIME1) + PRIME2;
    return (*src) >> 11;
}


static void generateU16 (U16* buffer, size_t buffSize, double p, U32* seed)
{
    U16 tableU16[PROBATABLESIZE];
    U32 remaining = PROBATABLESIZE;
    U32 pos = 0;
    U16* op = buffer;
    U16* const oend = op + buffSize;
    U16 val16 = 240;
    U16 max16 = 285;

    /* Build Symbol Table */
    while (remaining)
    {
        const U32 n = (U32) (remaining * p) + 1;
        const U32 end = pos + n;
        while (pos<end) tableU16[pos++]= val16;
        val16++;
        if (val16 > max16) val16 = 1;
        remaining -= n;
    }

    /* Fill buffer */
    while (op<oend)
    {
        const U32 r = FUZ_rand (seed) & (PROBATABLESIZE-1);
        *op++ = tableU16[r];
    }
}


static void FUZ_tests (U32 seed, U32 totalTest, U32 startTestNb)
{
    size_t bufferDstSize = BUFFERSIZE*sizeof(U16) + 64;
    U16* bufferP8    = (U16*) malloc (bufferDstSize);
    void* bufferDst  = malloc (bufferDstSize);
    U16* bufferVerif = (U16*) malloc (bufferDstSize);
    unsigned testNb;
    const size_t maxTestSizeMask = 0x1FFFF;
    U32 time = FUZ_GetMilliStart();

    generateU16 (bufferP8, BUFFERSIZE, 0.08, &seed);

    if (startTestNb)
    {
        U32 i;
        for (i=0; i<startTestNb; i++)
            FUZ_rand (&seed);
    }

    for (testNb=startTestNb; testNb<totalTest; testNb++)
    {
        U16* bufferTest;
        int tag=0;
        U32 roundSeed = seed ^ 0xEDA5B371;
        FUZ_rand(&seed);

        DISPLAYLEVEL (4, "\r test %5u  ", testNb);
        if (FUZ_GetMilliSpan (time) > FUZ_UPDATERATE)
        {
            DISPLAY ("\r test %5u  ", testNb);
            time = FUZ_GetMilliStart();
        }

        /* Compression / Decompression tests */
        {
            size_t sizeOrig = (FUZ_rand (&roundSeed) & maxTestSizeMask) + 1;
            size_t offset = (FUZ_rand(&roundSeed) % (BUFFERSIZE - 64 - maxTestSizeMask));
            size_t sizeCompressed;
            U64 hashOrig;
            bufferTest = bufferP8 + offset;

            DISPLAYLEVEL (4,"%3i ", tag++);;
            hashOrig = XXH64 (bufferTest, sizeOrig * sizeof(U16), 0);
            sizeCompressed = FSE_compressU16 (bufferDst, bufferDstSize, bufferTest, sizeOrig, FSE_MAX_SYMBOL_VALUE, 12);
            if (FSE_isError(sizeCompressed))
                DISPLAY ("\r test %5u : FSE_compressU16 failed ! \n", testNb);
            else if (sizeCompressed > 1)   /* don't check uncompressed & rle corner cases */
            {
                U16 saved = (bufferVerif[sizeOrig] = 1024 + 250);
                size_t result = FSE_decompressU16 (bufferVerif, sizeOrig, bufferDst, sizeCompressed);
                if (bufferVerif[sizeOrig] != saved)
                    DISPLAY ("\r test %5u : FSE_decompressU16 overrun output buffer (write beyond specified end) !\n", testNb);
                if (FSE_isError(result))
                    DISPLAY ("\r test %5u : FSE_decompressU16 failed ! \n", testNb);
                else
                {
                    U64 hashEnd = XXH64 (bufferVerif, sizeOrig * sizeof(U16), 0);
                    if (hashEnd != hashOrig) DISPLAY ("\r test %5u : Decompressed data corrupted !! \n", testNb);
                }
            }
        }
    }

    /* exit */
    free (bufferP8);
    free (bufferDst);
    free (bufferVerif);
}


/*****************************************************************
*  Unitary tests
*****************************************************************/
extern int FSE_countU16(unsigned* count, const unsigned short* source, unsigned sourceSize, unsigned* maxSymbolValuePtr);

#define CHECK(cond, ...) if (cond) { DISPLAY("Error => "); DISPLAY(__VA_ARGS__); \
                         DISPLAY(" (seed %u, test nb %u)  \n", seed, testNb); exit(-1); }
#define TBSIZE (16 KB)
static void unitTest(void)
{
    U16  testBuffU16[TBSIZE];
    size_t errorCode;
    U32 seed=0, testNb=0;

    /* FSE_countU16 */
    {
        U32 table[FSE_MAX_SYMBOL_VALUE+2];
        U32 max, i;

        for (i=0; i< TBSIZE; i++) testBuffU16[i] = i % (FSE_MAX_SYMBOL_VALUE+1);

        max = FSE_MAX_SYMBOL_VALUE;
        errorCode = FSE_countU16(table, testBuffU16, TBSIZE, &max);
        CHECK(FSE_isError(errorCode), "FSE_countU16() should have worked");

        max = FSE_MAX_SYMBOL_VALUE+1;
        errorCode = FSE_countU16(table, testBuffU16, TBSIZE, &max);
        CHECK(!FSE_isError(errorCode), "FSE_countU16() should have failed : max too large");

        max = FSE_MAX_SYMBOL_VALUE-1;
        errorCode = FSE_countU16(table, testBuffU16, TBSIZE, &max);
        CHECK(!FSE_isError(errorCode), "FSE_countU16() should have failed : max too low");
    }

    DISPLAY("Unit tests completed\n");
}


/*****************************************************************
*  Command line
*****************************************************************/
int main (int argc, char** argv)
{
    U32 seed, startTestNb=0, pause=0, totalTest = FUZ_NB_TESTS;
    int argNb;

    seed = FUZ_GetMilliStart() % 10000;
    DISPLAYLEVEL (1, "FSE U16 (%2i bits) automated test\n", (int)sizeof(void*)*8);
    for (argNb=1; argNb<argc; argNb++)
    {
        char* argument = argv[argNb];
        if (argument[0]=='-')
        {
            while (argument[1]!=0)
            {
                argument ++;
                switch (argument[0])
                {
                /* seed setting */
                case 's':
                    argument++;
                    seed=0;
                    while ((*argument>='0') && (*argument<='9'))
                    {
                        seed *= 10;
                        seed += *argument - '0';
                        argument++;
                    }
                    break;

                /* total nb fuzzer tests */
                case 'i':
                    argument++;
                    totalTest=0;
                    while ((*argument>='0') && (*argument<='9'))
                    {
                        totalTest *= 10;
                        totalTest += *argument - '0';
                        argument++;
                    }
                    break;

                /* jump to test nb */
                case 't':
                    argument++;
                    startTestNb=0;
                    while ((*argument>='0') && (*argument<='9'))
                    {
                        startTestNb *= 10;
                        startTestNb += *argument - '0';
                        argument++;
                    }
                    break;

                /* verbose mode */
                case 'v':
                    displayLevel=4;
                    break;

                /* pause (hidden) */
                case 'p':
                    pause=1;
                    break;

                default:
                    ;
                }
            }
        }
    }

    unitTest();

    DISPLAY("Fuzzer seed : %u \n", seed);
    FUZ_tests (seed, totalTest, startTestNb);

    DISPLAY ("\rAll tests passed               \n");
    if (pause)
    {
        DISPLAY("press enter ...\n");
        getchar();
    }
    return 0;
}
