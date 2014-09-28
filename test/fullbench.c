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
static U32 g_count[256] = {0};
static int local_trivialCount(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    const BYTE* ip = (BYTE*)src;
    const BYTE* const end = ip + srcSize;
    (void)dst; (void)dstSize;
    memset(g_count, 0, sizeof(g_count));
    while (ip<end) g_count[*ip++]++;
    return 0;
}

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
    return FSE_compress(dst, src, srcSize);
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
    (void)src; (void)srcSize; (void)dstSize;
    return FSE_writeHeader(dst, g_normTable, 255, g_tableLog);
}

static int local_FSE_buildCTable(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)dst; (void)dstSize; (void)src; (void)srcSize;
    return FSE_buildCTable(g_CTable, g_normTable, 255, g_tableLog);
}

static int local_FSE_compress_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    (void)dstSize;
    return FSE_compress_usingCTable(dst, src, srcSize, g_CTable);
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
            FSE_count(g_countTable, oBuffer, benchedSize, &max);
            g_tableLog = FSE_optimalTableLog(g_tableLog, (U32)benchedSize, max);
            funcName = "FSE_normalizeCount";
            func = local_FSE_normalizeCount;
            break;
        }

    case 5:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, benchedSize, &max);
            g_tableLog = FSE_optimalTableLog(g_tableLog, (U32)benchedSize, max);
            FSE_normalizeCount(g_normTable, g_tableLog, g_countTable, (U32)benchedSize, max);
            funcName = "FSE_writeHeader";
            func = local_FSE_writeHeader;
            break;
        }

    case 6:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, benchedSize, &max);
            g_tableLog = FSE_optimalTableLog(g_tableLog, (U32)benchedSize, max);
            FSE_normalizeCount(g_normTable, g_tableLog, g_countTable, (U32)benchedSize, max);
            funcName = "FSE_buildCTable";
            func = local_FSE_buildCTable;
            break;
        }

    case 7:
        {
            U32 max=255;
            FSE_count(g_countTable, oBuffer, benchedSize, &max);
            g_tableLog = FSE_normalizeCount(g_normTable, g_tableLog, g_countTable, (U32)benchedSize, max);
            FSE_buildCTable(g_CTable, g_normTable, max, g_tableLog);
            funcName = "FSE_compress_usingCTable";
            func = local_FSE_compress_usingCTable;
            break;
        }

    case 8:
        funcName = "FSE_compress";
        func = local_FSE_compress;
        break;

    /* Specific test functions */
    case 100:
        funcName = "trivialCount";
        func = local_trivialCount;
        break;

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
        DISPLAY("%1u-%-22.22s : \r", benchNb, funcName);
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
                if (errorCode < 0) exit(-1);
                loopNb++;
            }
            milliTime = BMK_GetMilliSpan(milliTime);
            averageTime = (double)milliTime / loopNb;
            if (averageTime < bestTime) bestTime = averageTime;
            DISPLAY("%1u-%-22.22s : %8.1f MB/s\r", benchNb+1, funcName, (double)benchedSize / bestTime / 1000.);
        }
        DISPLAY("%-24.24s : %8.1f MB/s   (%i)\n", funcName, (double)benchedSize / bestTime / 1000., (int)errorCode);
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
        for (i=1; i<=8; i++)
            result = fullSpeedBench((double)proba / 100, nbLoops, i);
    }
    else
        result = fullSpeedBench((double)proba / 100, nbLoops, algNb);

    if (pause) { DISPLAY("press enter...\n"); getchar(); }

    return result;
}

