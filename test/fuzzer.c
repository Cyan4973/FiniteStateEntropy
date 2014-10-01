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
#include "fse.h"
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


//**************************************
// Macros
//**************************************
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }


//***************************************************
// Local variables
//***************************************************
static char* programName;
static int   displayLevel = 2;   // 0 : no display  // 1: errors  // 2 : + result + interaction + warnings ;  // 3 : + progression;  // 4 : + information


//******************************
// local functions
//******************************
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
    // Fill buffer
    while (op<oend) *op++ = (BYTE)FUZ_rand(seed);
}


static int FUZ_checkCount (short* normalizedCount, int tableLog, int maxSV)
{
    int total = 1<<tableLog;
    int count = 0;
    int i;
    if (tableLog > 31) return -1;
    for (i=0; i<=maxSV; i++)
        count += abs(normalizedCount[i]);
    if (count != total) return -1;
    return 0;
}


static void FUZ_tests (U32 seed, U32 totalTest, U32 startTestNb)
{
    BYTE* bufferNoise = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferSrc   = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferDst   = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferVerif = (BYTE*) malloc (BUFFERSIZE+64);
    unsigned testNb, maxSV, tableLog;
    U32 time = FUZ_GetMilliStart();
    const U32 nbRandPerLoop = 4;

    generate (bufferSrc, BUFFERSIZE, 0.1, &seed);
    generateNoise (bufferNoise, BUFFERSIZE, &seed);

    if (startTestNb)
    {
        U32 i;
        for (i=0; i<nbRandPerLoop*startTestNb; i++)
            FUZ_rand (&seed);
    }

    for (testNb=startTestNb; testNb<totalTest; testNb++)
    {
        int tag=0;
        DISPLAYLEVEL (4, "\r test %5u  ", testNb);
        if (FUZ_GetMilliSpan (time) > FUZ_UPDATERATE)
        {
            DISPLAY ("\r test %5u  ", testNb);
            time = FUZ_GetMilliStart();
        }

        /* Noise Compression test */
        {
            int sizeOrig = (FUZ_rand (&seed) & 0x1FFFF) + 1;
            int sizeCompressed;
            BYTE* bufferTest = bufferNoise + testNb;
            DISPLAYLEVEL (4,"%3i\b\b\b", tag++);;
            sizeCompressed = FSE_compress (bufferDst, bufferTest, sizeOrig);
            if (sizeCompressed == -1)
                DISPLAY ("Noise Compression failed ! \n");
            if (sizeCompressed > sizeOrig+1)
                DISPLAY ("Noise Compression result too large !\n");
        }

        /* Compression / Decompression test */
        {
            int sizeOrig = (FUZ_rand (&seed) & 0x1FFFF) + 1;
            int sizeCompressed;
            U32 hashOrig;
            BYTE* bufferTest = bufferSrc + testNb;
            DISPLAYLEVEL (4,"%3i\b\b\b", tag++);;
            hashOrig = XXH32 (bufferTest, sizeOrig, 0);
            sizeCompressed = FSE_compress (bufferDst, bufferTest, sizeOrig);
            if (sizeCompressed == -1)
                DISPLAY ("Compression failed ! \n");
            else
            {
                BYTE saved = (bufferVerif[sizeOrig] = 254);
                int result = FSE_decompress_safe (bufferVerif, sizeOrig, bufferDst, sizeCompressed);
                if (bufferVerif[sizeOrig] != saved)
                    DISPLAY ("Output buffer (bufferVerif) overrun (write beyond specified end) !\n");
                if ((result==-1) && (sizeCompressed>=2))
                    DISPLAY ("Decompression failed ! \n");
                else
                {
                    U32 hashEnd = XXH32 (bufferVerif, sizeOrig, 0);
                    if (hashEnd != hashOrig) DISPLAY ("Decompressed data corrupted !! \n");
                }
            }
        }

        /* check header read function*/
        {
            BYTE* bufferTest = bufferSrc + testNb;   // Read some random noise
            short count[256];
            int result;
            DISPLAYLEVEL (4,"%3i\b\b\b", tag++);
            result = FSE_readHeader (count, &maxSV, &tableLog, bufferTest);
            if (result != -1)
            {
                result = FUZ_checkCount (count, tableLog, maxSV);
                if (result==-1)
                    DISPLAY ("symbol distribution corrupted !\n");
            }
        }

        /* Attempt decompression on bogus data*/
        {
            int sizeOrig = FUZ_rand (&seed) & 0x1FFFF;
            int sizeCompressed = FUZ_rand (&seed) & 0x1FFFF;
            BYTE* bufferTest = bufferSrc + testNb;
            BYTE saved = (bufferDst[sizeOrig] = 253);
            int result;
            DISPLAYLEVEL (4,"%3i\b\b\b", tag++);;
            result = FSE_decompress_safe (bufferDst, sizeOrig, bufferTest, sizeCompressed);
            if (bufferDst[sizeOrig] != saved)
                DISPLAY ("Output buffer bufferDst corrupted !\n");
            if (result != -1)
                if (! ( (*bufferTest==0) || (*bufferTest==1) ) )
                    DISPLAY ("Decompression completed ??\n");
        }
    }

    // exit
    free (bufferNoise);
    free (bufferSrc);
    free (bufferDst);
    free (bufferVerif);
}


/*****************************************************************
   Unitary tests
*****************************************************************/
extern int FSE_countU16(unsigned* count, const unsigned short* source, unsigned sourceSize, unsigned* maxSymbolValuePtr);

#define CHECK(cond, ...) if (cond) { DISPLAY("Error => "); DISPLAY(__VA_ARGS__); \
                         DISPLAY(" (seed %u, test nb %u)  \n", seed, testNb); exit(-1); }
#define TBSIZE (16 KB)
static void unitTest(void)
{
    BYTE testBuff[TBSIZE];
    U32 i;
    int errorCode;
    U32 seed=0, testNb=0;

    // FSE_count
    {
        U32 table[256];
        U32 max;
        for (i=0; i< TBSIZE; i++) testBuff[i] = i % 127;
        max = 128;
        errorCode = FSE_count(table, testBuff, TBSIZE, &max);
        CHECK(errorCode<0, "Error : FSE_count() should have worked");
        max = 124;
        errorCode = FSE_count(table, testBuff, TBSIZE, &max);
        CHECK(errorCode>=0, "Error : FSE_count() should have failed : value > max");
        max = 65000;
        errorCode = FSE_count(table, testBuff, TBSIZE, &max);
        CHECK(errorCode<0, "Error : FSE_count() should have worked");
    }

    // FSE_countU16
    {
        U32 table[FSE_MAX_SYMBOL_VALUE+2];
        U32 max;
        U16* tbu16 = (U16*)testBuff;
        unsigned tbu16Size = TBSIZE / 2;

        max = 124;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(errorCode>=0, "Error : FSE_countU16() should have failed : value too large");

        for (i=0; i< tbu16Size; i++) tbu16[i] = i % (FSE_MAX_SYMBOL_VALUE+1);

        max = FSE_MAX_SYMBOL_VALUE;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(errorCode<0, "Error : FSE_countU16() should have worked");

        max = FSE_MAX_SYMBOL_VALUE+1;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(errorCode>=0, "Error : FSE_countU16() should have failed : max too large");

        max = FSE_MAX_SYMBOL_VALUE-1;
        errorCode = FSE_countU16(table, tbu16, tbu16Size, &max);
        CHECK(errorCode>=0, "Error : FSE_countU16() should have failed : max too low");
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
        CHECK(errorCode<0, "Error : FSE_count() should have worked");
        tableLog = FSE_optimalTableLog(0, TBSIZE, max);
        errorCode = FSE_normalizeCount(norm, tableLog, count, TBSIZE, max);
        CHECK(errorCode<0, "Error : FSE_normalizeCount() should have worked");

        errorCode = FSE_writeHeader(header, 513, norm, max, tableLog);
        CHECK(errorCode<0, "Error : FSE_writeHeader() should have worked");

        errorCode = FSE_writeHeader(header, errorCode+1, norm, max, tableLog);
        CHECK(errorCode<0, "Error : FSE_writeHeader() should have worked");

        errorCode = FSE_writeHeader(header, errorCode-1, norm, max, tableLog);
        CHECK(errorCode>=0, "Error : FSE_writeHeader() should have failed");
    }

    DISPLAY("Unit tests completed\n");
}


/*****************************************************************
   Command line
*****************************************************************/
int main (int argc, char** argv)
{
    U32 seed, startTestNb=0, pause=0, totalTest = FUZ_NB_TESTS;
    int argNb;

    programName = argv[0];
    seed = FUZ_GetMilliStart() % 10000;
    DISPLAYLEVEL (0, "FSE (%2i bits) automated test\n", (int)sizeof(void*)*8);
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
