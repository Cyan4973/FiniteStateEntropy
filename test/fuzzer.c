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
#include <stdlib.h>    // malloc
#include <stdio.h>     // printf
#include <string.h>    // memset
#include <sys/timeb.h> // timeb
#include "fse.h"
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
#define MB *(1<<20)
#define BUFFERSIZE ((1 MB) - 1)
#define FUZ_NB_TESTS  65536
#define PROBATABLESIZE 4096
#define FUZ_UPDATERATE  250
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
static int FUZ_GetMilliStart()
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


static unsigned int FUZ_rand (unsigned int* src)
{
    *src =  ( (*src) * PRIME1) + PRIME2;
    return (*src) >>7;
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


static int FUZ_checkCount (U32* normalizedCount, int tableLog, int nbSymbols)
{
    int total = 1<<tableLog;
    int count = 0;
    int i;
    if (tableLog > 31) return -1;
    for (i=0; i<nbSymbols; i++)
        count += normalizedCount[i];
    if (count != total) return -1;
    return 0;
}


static void FUZ_tests (U32 seed, U32 startTestNb)
{
    BYTE* bufferSrc   = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferDst   = (BYTE*) malloc (BUFFERSIZE+64);
    BYTE* bufferVerif = (BYTE*) malloc (BUFFERSIZE+64);
    int testNb, nbSymbols, tableLog;
    U32 time = FUZ_GetMilliStart();
    const U32 nbRandPerLoop = 3;

    generate (bufferSrc, BUFFERSIZE, 0.1, &seed);

    if (startTestNb)
    {
        U32 i;
        for (i=0; i<nbRandPerLoop*startTestNb; i++)
            FUZ_rand (&seed);
    }

    for (testNb=startTestNb; testNb<FUZ_NB_TESTS; testNb++)
    {
        int tag=0;
        DISPLAYLEVEL (4, "\r test %5i  ", testNb);
        if (FUZ_GetMilliSpan (time) > FUZ_UPDATERATE)
        {
            DISPLAY ("\r test %5i  ", testNb);
            time = FUZ_GetMilliStart();
        }

        /* Compression / Decompression test */
        {
            int sizeOrig = FUZ_rand (&seed) & 0x1FFFF;
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
                BYTE saved = bufferVerif[sizeOrig];
                int result = FSE_decompress_safe (bufferVerif, sizeOrig, bufferDst, sizeCompressed);
                if (bufferVerif[sizeOrig] != saved)
                    DISPLAY ("Output buffer bufferVerif corrupted !\n");
                if (result==-1)
                    DISPLAY ("Decompression failed ! \n");
                else
                {
                    U32 hashEnd = XXH32 (bufferVerif, sizeOrig, 0);
                    if (hashEnd != hashOrig) DISPLAY ("Data corrupted !! \n");
                }
            }
        }

        /* check header read*/
        {
            BYTE* bufferTest = bufferSrc + testNb;
            U32 count[256];
            int result = FSE_readHeader (count, &nbSymbols, &tableLog, bufferTest);
            DISPLAYLEVEL (4,"%3i\b\b\b", tag++);;
            if (result != -1)
            {
                result = FUZ_checkCount (count, tableLog, nbSymbols);
                if (result==-1)
                    DISPLAY ("symbol distribution corrupted !\n");
            }
        }

        /* Attempt decompression on bogus data*/
        {
            int sizeOrig = FUZ_rand (&seed) & 0x1FFFF;
            int sizeCompressed = FUZ_rand (&seed) & 0x1FFFF;
            BYTE* bufferTest = bufferSrc + testNb;
            BYTE saved = bufferDst[sizeOrig];
            int result = FSE_decompress_safe (bufferDst, sizeOrig, bufferTest, sizeCompressed);
            DISPLAYLEVEL (4,"%3i\b\b\b", tag++);;
            if (bufferDst[sizeOrig] != saved)
                DISPLAY ("Output buffer bufferDst corrupted !\n");
            if (result != -1)
                if (! ( (*bufferTest==0) || (*bufferTest==1) ) )
                    DISPLAY ("Decompression completed ??\n");
        }
    }

    // exit
    free (bufferDst);
    free (bufferSrc);
    free (bufferVerif);
}


/*****************************************************************
   Command line
*****************************************************************/
int main (int argc, char** argv)
{
    char userInput[80] = {0};
    U32 seed, startTestNb=0;
    U32 timestamp=FUZ_GetMilliStart();
    int argNb;

    programName = argv[0];
    DISPLAYLEVEL (0, "FSE automated test\n");
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
                    // verbose mode
                case 'v':
                    displayLevel=4;
                    break;
                default:
                    ;
                }
            }
        }
    }

    DISPLAY ("Select an Initialisation number (default : random) : ");
    fflush (stdout);
    if ( fgets (userInput, sizeof (userInput), stdin) )
    {
        if ( sscanf (userInput, "%d", &seed) == 1 )
        {
            DISPLAY ("Select start test nb (default : 0) : ");
            fflush (stdout);
            if ( fgets (userInput, sizeof (userInput), stdin) )
            {
                if ( sscanf (userInput, "%d", &startTestNb) == 1 ) {}
                else startTestNb=0;
            }
        }
        else seed = FUZ_GetMilliSpan (timestamp);
    }
    printf ("Seed = %u\n", seed);

    FUZ_tests (seed, startTestNb);

    DISPLAY ("\rAll tests passed               \n");
    DISPLAY ("Press enter to exit \n");
    getchar();
    return 0;
}
