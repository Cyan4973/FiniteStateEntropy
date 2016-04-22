/*
FuzzerHuff0.c
Automated test program for HUF
Copyright (C) Yann Collet 2015

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
- FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
- Public forum : https://groups.google.com/forum/#!forum/lz4c
*/


/******************************
*  Compiler options
******************************/
#define _CRT_SECURE_NO_WARNINGS   /* Visual warning */


/******************************
*  Include
*******************************/
#include <stdlib.h>     /* malloc, abs */
#include <stdio.h>      /* printf */
#include <string.h>     /* memset */
#include <sys/timeb.h>  /* timeb */
#include "mem.h"
#include "huf_static.h"
#include "xxhash.h"


/***************************************************
*  Constants
***************************************************/
#define KB *(1<<10)
#define MB *(1<<20)
#define BUFFERSIZE ((1 MB) - 1)
#define FUZ_NB_TESTS  (128 KB)
#define PROBATABLESIZE (4 KB)
#define FUZ_UPDATERATE  200
#define PRIME1   2654435761U
#define PRIME2   2246822519U


/***************************************************
*  Macros
***************************************************/
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static unsigned displayLevel = 2;   /* 0 : no display; 1: errors; 2 : + result + interaction + warnings; 3 : + progression; 4 : + information */


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


static void generate (void* buffer, size_t buffSize, double p, U32* seed)
{
    char table[PROBATABLESIZE] = {0};
    int remaining = PROBATABLESIZE;
    int pos = 0;
    int s = 0;
    char* op = (char*) buffer;
    char* oend = op + buffSize;

    /* Build Table */
    while (remaining)
    {
        int n = (int) (remaining * p);
        int end;
        if (!n) n=1;
        end = pos + n;
        while (pos<end) table[pos++]= (char) s;
        s++;
        remaining -= n;
    }

    /* Fill buffer */
    while (op<oend)
    {
        const int r = FUZ_rand (seed) & (PROBATABLESIZE-1);
        *op++ = table[r];
    }
}


static void generateNoise (void* buffer, size_t buffSize, U32* seed)
{
    BYTE* op = (BYTE*)buffer;
    BYTE* const oend = op + buffSize;
    while (op<oend) *op++ = (BYTE)FUZ_rand(seed);
}


#define CHECK(cond, ...) if (cond) { DISPLAY("Error => "); DISPLAY(__VA_ARGS__); \
                         DISPLAY(" (seed %u, test nb %u)  \n", seed, testNb); exit(-1); }

static void FUZ_tests (U32 seed, U32 totalTest, U32 startTestNb)
{
    BYTE* bufferP0    = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferP1    = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferP15   = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferP90   = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferP100  = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferDst   = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferVerif = (BYTE*) malloc (BUFFERSIZE+64);
    size_t bufferDstSize = BUFFERSIZE+64;
    unsigned testNb;
    const size_t maxTestSizeMask = 0x1FFFF;   /* 128 KB - 1 */
    U32 rootSeed = seed;
    U32 time = FUZ_GetMilliStart();

    generateNoise (bufferP0, BUFFERSIZE, &rootSeed);
    generate (bufferP1  , BUFFERSIZE, 0.01, &rootSeed);
    generate (bufferP15 , BUFFERSIZE, 0.15, &rootSeed);
    generate (bufferP90 , BUFFERSIZE, 0.90, &rootSeed);
    memset(bufferP100, (BYTE)FUZ_rand(&rootSeed), BUFFERSIZE);
    memset(bufferDst, 0, BUFFERSIZE);

    if (startTestNb)
    {
        U32 i;
        for (i=0; i<startTestNb; i++)
            FUZ_rand (&rootSeed);
    }

    for (testNb=startTestNb; testNb<totalTest; testNb++)
    {
        BYTE* bufferTest = NULL;
        int tag=0;
        U32 roundSeed = rootSeed ^ 0xEDA5B371;
        FUZ_rand(&rootSeed);

        DISPLAYLEVEL (4, "\r test %5u  ", testNb);
        if (FUZ_GetMilliSpan (time) > FUZ_UPDATERATE)
        {
            DISPLAY ("\r test %5u  ", testNb);
            time = FUZ_GetMilliStart();
        }

        /* Compression / Decompression tests */
        {
            /* determine test sample */
            size_t sizeOrig = (FUZ_rand(&roundSeed) & maxTestSizeMask) + 1;
            size_t offset = (FUZ_rand(&roundSeed) % (BUFFERSIZE - 64 - maxTestSizeMask));
            size_t sizeCompressed;
            U32 hashOrig;

            if (FUZ_rand(&roundSeed) & 7) bufferTest = bufferP15 + offset;
            else
            {
                switch(FUZ_rand(&roundSeed) & 3)
                {
                    case 0: bufferTest = bufferP0 + offset; break;
                    case 1: bufferTest = bufferP1 + offset; break;
                    case 2: bufferTest = bufferP90 + offset; break;
                    default : bufferTest = bufferP100 + offset; break;
                }
            }
            DISPLAYLEVEL (4,"%3i ", tag++);
            hashOrig = XXH32 (bufferTest, sizeOrig, 0);

            /* compression test */
            sizeCompressed = HUF_compress (bufferDst, bufferDstSize, bufferTest, sizeOrig);
            CHECK(HUF_isError(sizeCompressed), "Compression failed");
            if (sizeCompressed > 1)   /* don't check uncompressed & rle corner cases */
            {
                /* failed compression test */
                {
                    BYTE saved = (bufferVerif[sizeCompressed-1] = 253);
                    size_t errorCode;
                    errorCode = HUF_compress (bufferVerif, sizeCompressed-1, bufferTest, sizeOrig);
                    CHECK(errorCode!=0, "compression should have failed (too small destination buffer)")
                    CHECK(bufferVerif[sizeCompressed-1] != saved, "HUF_compress w/ too small dst : bufferVerif overflow");
                }

                /* decompression test */
                {
                    U32 hashEnd;
                    BYTE saved = (bufferVerif[sizeOrig] = 253);
                    size_t result = HUF_decompress (bufferVerif, sizeOrig, bufferDst, sizeCompressed);
                    CHECK(bufferVerif[sizeOrig] != saved, "HUF_decompress : bufferVerif overflow");
                    CHECK(HUF_isError(result), "Decompression failed : %s", HUF_getErrorName(result));
                    hashEnd = XXH32 (bufferVerif, sizeOrig, 0);
                    CHECK(hashEnd != hashOrig, "Decompressed data corrupted");
                    CHECK(bufferVerif[sizeOrig] != saved, "HUF_decompress overwrite dst buffer !");
                }

                /* truncated src decompression test */
                if (sizeCompressed>4)
                {
                    /* note : in some rare cases, the truncated bitStream may still generate by chance a valid output of correct size.
                              It typically requires `missing` to be small; hence a minimum value of `missing` for this test */
                    size_t errorCode;
                    const size_t missing = (FUZ_rand(&roundSeed) % (sizeCompressed-3)) + 2;   /* no problem, as sizeCompressed > 4 */
                    const size_t tooSmallSize = sizeCompressed - missing;
                    void* cBufferTooSmall = malloc(tooSmallSize);   /* valgrind will catch read overflows */
                    CHECK(cBufferTooSmall == NULL, "not enough memory !");
                    memcpy(cBufferTooSmall, bufferDst, tooSmallSize);
                    errorCode = HUF_decompress(bufferVerif, sizeOrig, cBufferTooSmall, tooSmallSize);
                    CHECK(!HUF_isError(errorCode) && (errorCode!=sizeOrig), "HUF_decompress should have failed ! (truncated src buffer)");
                    free(cBufferTooSmall);
                }
            }
        }

        /* Attempt decompression on bogus data */
        {
            size_t maxDstSize = FUZ_rand (&roundSeed) & maxTestSizeMask;
            size_t sizeCompressed = FUZ_rand (&roundSeed) & maxTestSizeMask;
            BYTE saved = (bufferDst[maxDstSize] = 253);
            size_t result;
            DISPLAYLEVEL (4,"\b\b\b\b%3i ", tag++);;
            result = HUF_decompress (bufferDst, maxDstSize, bufferTest, sizeCompressed);
            CHECK(!HUF_isError(result) && (result > maxDstSize), "Decompression overran output buffer");
            CHECK(bufferDst[maxDstSize] != saved, "HUF_decompress noise : bufferDst overflow");
        }
    }

    /* exit */
    free (bufferP0);
    free (bufferP1);
    free (bufferP15);
    free (bufferP90);
    free (bufferP100);
    free (bufferDst);
    free (bufferVerif);
}


/*****************************************************************
*  Unitary tests
*****************************************************************/
#define TBSIZE (16 KB)
static void unitTest(void)
{
    BYTE* testBuff = (BYTE*)malloc(TBSIZE);
    BYTE* cBuff = (BYTE*)malloc(HUF_COMPRESSBOUND(TBSIZE));
    BYTE* verifBuff = (BYTE*)malloc(TBSIZE);

    if ((!testBuff) || (!cBuff) || (!verifBuff))
    {
        DISPLAY("Not enough memory, exiting ... \n");
        free(testBuff);
        free(cBuff);
        free(verifBuff);
        return;
    }

    /* Targeted test 1 */
    {
    }

    /* Targeted test 2 */
    {
    }

    free(testBuff);
    free(cBuff);
    free(verifBuff);
    DISPLAY("Unit tests completed\n");
}


/*****************************************************************
*  Command line
*****************************************************************/

int badUsage(const char* exename)
{
    (void) exename;
    DISPLAY("wrong parameter\n");
    return 1;
}


int main (int argc, char** argv)
{
    U32 seed, startTestNb=0, pause=0, totalTest = FUZ_NB_TESTS;
    int argNb;

    seed = FUZ_GetMilliStart() % 10000;
    DISPLAYLEVEL (1, "HUF (%2i bits) automated test\n", (int)sizeof(void*)*8);
    for (argNb=1; argNb<argc; argNb++)
    {
        char* argument = argv[argNb];
        if (argument[0]=='-')
        {
            argument++;
            while (argument[0]!=0)
            {
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

                /* total tests */
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
                    argument++;
                    displayLevel=4;
                    break;

                /* pause (hidden) */
                case 'p':
                    argument++;
                    pause=1;
                    break;

                default:
                    return badUsage(argv[0]);
                }
            }
        }
    }

    if (startTestNb == 0) unitTest();

    DISPLAY("Fuzzer seed : %u \n", seed);
    FUZ_tests (seed, totalTest, startTestNb);

    DISPLAY ("\rAll %u tests passed               \n", totalTest);
    if (pause)
    {
        int unused;
        DISPLAY("press enter ...\n");
        unused = getchar();
        (void)unused;
    }
    return 0;
}
