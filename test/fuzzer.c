/*
Fuzzer.c
Automated test program for FSE
Copyright (C) Yann Collet 2012-2013
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


//******************************
// Compiler options
//******************************
#define _CRT_SECURE_NO_WARNINGS   // Visual warning


//******************************
// Include
//******************************
#include <stdlib.h>    // malloc, abs
#include <stdio.h>     // printf
#include <string.h>    // memset
#include <sys/timeb.h> // timeb
#include "fse_static.h"
#include "fseU16.h"
#include "xxhash.h"


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
#else
typedef unsigned char       BYTE;
typedef unsigned short      U16;
typedef   signed short      S16;
typedef unsigned int        U32;
typedef   signed int        S32;
typedef unsigned long long  U64;
#endif


//******************************
// Constants
//******************************
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


static void generate (void* buffer, size_t buffSize, double p, U32* seed)
{
    char table[PROBATABLESIZE];
    int remaining = PROBATABLESIZE;
    int pos = 0;
    int s = 0;
    char* op = (char*) buffer;
    char* oend = op + buffSize;

    // Build Table
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

    // Fill buffer
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


static int FUZ_checkCount (short* normalizedCount, int tableLog, int maxSV)
{
    int total = 1<<tableLog;
    int count = 0;
    int i;
    if (tableLog > 20) return -1;
    for (i=0; i<=maxSV; i++)
        count += abs(normalizedCount[i]);
    if (count != total) return -1;
    return 0;
}


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
    unsigned testNb, maxSV, tableLog;
    U32 time = FUZ_GetMilliStart();

    generateNoise (bufferP0, BUFFERSIZE, &seed);
    generate (bufferP1  , BUFFERSIZE, 0.01, &seed);
    generate (bufferP15 , BUFFERSIZE, 0.15, &seed);
    generate (bufferP90 , BUFFERSIZE, 0.90, &seed);
    memset(bufferP100, (BYTE)FUZ_rand(&seed), BUFFERSIZE);

    if (startTestNb)
    {
        U32 i;
        for (i=0; i<startTestNb; i++)
            FUZ_rand (&seed);
    }

    for (testNb=startTestNb; testNb<totalTest; testNb++)
    {
        BYTE* bufferTest;
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
            int sizeOrig = (FUZ_rand (&roundSeed) & 0x1FFFF) + 1;
            size_t sizeCompressed;
            U32 hashOrig;

            if (FUZ_rand(&roundSeed) & 7) bufferTest = bufferP15 + testNb;
            else
            {
                switch(FUZ_rand(&roundSeed) & 3)
                {
                    case 0: bufferTest = bufferP0 + testNb; break;
                    case 1: bufferTest = bufferP1 + testNb; break;
                    case 2: bufferTest = bufferP90 + testNb; break;
                    default : bufferTest = bufferP100 + testNb; break;
                }
            }
            DISPLAYLEVEL (4,"%3i ", tag++);;
            hashOrig = XXH32 (bufferTest, sizeOrig, 0);
            sizeCompressed = FSE_compress (bufferDst, bufferDstSize, bufferTest, sizeOrig);
            if (FSE_isError(sizeCompressed))
                DISPLAY ("\r test %5u : Compression failed ! \n", testNb);
            else if (sizeCompressed > 1)   /* don't check uncompressed & rle corner cases */
            {
                BYTE saved = (bufferVerif[sizeOrig] = 254);
                size_t result = FSE_decompress (bufferVerif, sizeOrig, bufferDst, sizeCompressed);
                if (bufferVerif[sizeOrig] != saved)
                    DISPLAY ("\r test %5u : Output buffer (bufferVerif) overrun (write beyond specified end) !\n", testNb);
                if (FSE_isError(result))
                    DISPLAY ("\r test %5u : Decompression failed ! \n", testNb);
                else
                {
                    U32 hashEnd = XXH32 (bufferVerif, sizeOrig, 0);
                    if (hashEnd != hashOrig) DISPLAY ("\r test %5u : Decompressed data corrupted !! \n", testNb);
                }
            }
        }

        /* Attempt header decoding on bogus data */
        {
            short count[256];
            size_t result;
            DISPLAYLEVEL (4,"\b\b\b\b%3i ", tag++);
            maxSV = 256;
            result = FSE_readHeader (count, &maxSV, &tableLog, bufferTest, FSE_MAX_HEADERSIZE);
            if (!FSE_isError(result))
            {
                int check;
                if (maxSV > 255)
                    DISPLAY ("\r test %5u : count table overflow (%u)!\n", testNb, maxSV+1);
                check = FUZ_checkCount (count, tableLog, maxSV);
                if (check==-1)
                    DISPLAY ("\r test %5u : symbol distribution corrupted !\n", testNb);
            }
        }

        /* Attempt decompression on bogus data */
        {
            size_t maxDstSize = FUZ_rand (&roundSeed) & 0x1FFFF;
            size_t sizeCompressed = FUZ_rand (&roundSeed) & 0x1FFFF;
            BYTE saved = (bufferDst[maxDstSize] = 253);
            size_t result;
            DISPLAYLEVEL (4,"\b\b\b\b%3i ", tag++);;
            result = FSE_decompress (bufferDst, maxDstSize, bufferTest, sizeCompressed);
            if (!FSE_isError(result))
            {
                if (result > maxDstSize) DISPLAY ("\r test %5u : Decompression overrun output buffer\n", testNb);
            }
            if (bufferDst[maxDstSize] != saved)
                DISPLAY ("\r test %5u : Output buffer bufferDst corrupted !\n", testNb);
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
extern int FSE_countU16(unsigned* count, const unsigned short* source, unsigned sourceSize, unsigned* maxSymbolValuePtr);

#define CHECK(cond, ...) if (cond) { DISPLAY("Error => "); DISPLAY(__VA_ARGS__); \
                         DISPLAY(" (seed %u, test nb %u)  \n", seed, testNb); exit(-1); }
#define TBSIZE (16 KB)
static void unitTest(void)
{
    BYTE testBuff[TBSIZE];
    U32 i;
    size_t errorCode;
    U32 seed=0, testNb=0;

    // FSE_count
    {
        U32 table[256];
        U32 max;
        for (i=0; i< TBSIZE; i++) testBuff[i] = i % 127;
        max = 128;
        errorCode = FSE_count(table, testBuff, TBSIZE, &max);
        CHECK(FSE_isError(errorCode), "Error : FSE_count() should have worked");
        max = 124;
        errorCode = FSE_count(table, testBuff, TBSIZE, &max);
        CHECK(!FSE_isError(errorCode), "Error : FSE_count() should have failed : value > max");
        max = 65000;
        errorCode = FSE_count(table, testBuff, TBSIZE, &max);
        CHECK(FSE_isError(errorCode), "Error : FSE_count() should have worked");
    }

    // FSE_countU16
    {
        U32 table[FSE_MAX_SYMBOL_VALUE+2];
        U32 max;
        U16* tbu16 = (U16*)testBuff;
        unsigned tbu16Size = TBSIZE / 2;

        max = 124;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(!FSE_isError(errorCode), "Error : FSE_countU16() should have failed : value too large");

        for (i=0; i< tbu16Size; i++) tbu16[i] = i % (FSE_MAX_SYMBOL_VALUE+1);

        max = FSE_MAX_SYMBOL_VALUE;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(FSE_isError(errorCode), "Error : FSE_countU16() should have worked");

        max = FSE_MAX_SYMBOL_VALUE+1;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(!FSE_isError(errorCode), "Error : FSE_countU16() should have failed : max too large");

        max = FSE_MAX_SYMBOL_VALUE-1;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(!FSE_isError(errorCode), "Error : FSE_countU16() should have failed : max too low");
    }

    // FSE_writeHeader
    {
        U32 count[129];
        S16 norm[129];
        BYTE header[513];
        U32 max, tableLog;

        for (i=0; i< TBSIZE; i++) testBuff[i] = i % 127;
        max = 128;
        errorCode = FSE_count(count, testBuff, TBSIZE, &max);
        CHECK(FSE_isError(errorCode), "Error : FSE_count() should have worked");
        tableLog = FSE_optimalTableLog(0, TBSIZE, max);
        errorCode = FSE_normalizeCount(norm, tableLog, count, TBSIZE, max);
        CHECK(FSE_isError(errorCode), "Error : FSE_normalizeCount() should have worked");

        errorCode = FSE_writeHeader(header, 513, norm, max, tableLog);
        CHECK(FSE_isError(errorCode), "Error : FSE_writeHeader() should have worked");

        errorCode = FSE_writeHeader(header, errorCode+1, norm, max, tableLog);
        CHECK(FSE_isError(errorCode), "Error : FSE_writeHeader() should have worked");

        errorCode = FSE_writeHeader(header, errorCode-1, norm, max, tableLog);
        CHECK(!FSE_isError(errorCode), "Error : FSE_writeHeader() should have failed");
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
    DISPLAYLEVEL (1, "FSE (%2i bits) automated test\n", (int)sizeof(void*)*8);
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
                // seed setting
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

                // total tests
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

                // jumpt to test nb
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

                // verbose mode
                case 'v':
                    displayLevel=4;
                    break;

                // pause (hidden)
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
