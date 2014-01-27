/*
bench.c - Demo program to benchmark open-source compression algorithm
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

//**************************************
// Compiler Options
//**************************************
// Disable some Visual warning messages
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE     // VS2005

// Unix Large Files support (>4GB)
#define _FILE_OFFSET_BITS 64
#if (defined(__sun__) && (!defined(__LP64__)))   // Sun Solaris 32-bits requires specific definitions
#  define _LARGEFILE_SOURCE
#elif ! defined(__LP64__)                        // No point defining Large file for 64 bit
#  define _LARGEFILE64_SOURCE
#endif

// S_ISREG & gettimeofday() are not supported by MSVC
#if defined(_MSC_VER) || defined(_WIN32)
#  define BMK_LEGACY_TIMER 1
#endif


//**************************************
// Includes
//**************************************
#include <stdlib.h>      // malloc
#include <stdio.h>       // fprintf, fopen, ftello64
#include <string.h>      // strcat
#include <sys/types.h>   // stat64
#include <sys/stat.h>    // stat64

// Use ftime() if gettimeofday() is not available on your target
#if defined(BMK_LEGACY_TIMER)
#  include <sys/timeb.h>   // timeb, ftime
#else
#  include <sys/time.h>    // gettimeofday
#endif

#include "bench.h"
#include "fileio.h"
#include "../fse.h"
#include "xxhash.h"
#include "lz4hce.h"
#include "fseDist.h"

//**************************************
// Compiler specifics
//**************************************
#if !defined(S_ISREG)
#  define S_ISREG(x) (((x) & S_IFMT) == S_IFREG)
#endif

// GCC does not support _rotl outside of Windows
#if !defined(_WIN32)
#  define _rotl(x,r) ((x << r) | (x >> (32 - r)))
#endif

#if defined(_MSC_VER)   // Visual
#  pragma warning(disable : 4127)        // disable: C4127: conditional expression is constant
#  include <intrin.h>
#endif


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


//**************************************
// Constants
//**************************************
#define NBLOOPS    4
#define TIMELOOP   2500

#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

#define KNUTH               2654435761U
#define MAX_MEM             (2 GB - 64 MB)
#define DEFAULT_CHUNKSIZE   (32 KB)


//**************************************
// MACRO
//**************************************
#define DISPLAY(...) fprintf(stderr, __VA_ARGS__)


//**************************************
// Benchmark Parameters
//**************************************
static int chunkSize = DEFAULT_CHUNKSIZE;
static int nbIterations = NBLOOPS;
static int BMK_pause = 0;

void BMK_SetBlocksize(int bsize) { chunkSize = bsize; }

void BMK_SetNbIterations(int nbLoops)
{
    nbIterations = nbLoops;
    DISPLAY("- %i iterations -\n", nbIterations);
}

typedef struct
{
    unsigned int id;
    char* origBuffer;
    char* compressedBuffer;
    int   origSize;
    int   compressedSize;
} chunkParameters_t;


//*********************************************************
//  Private functions
//*********************************************************

#if defined(BMK_LEGACY_TIMER)

static int BMK_GetMilliStart()
{
    // Based on Legacy ftime()
    // Rolls over every ~ 12.1 days (0x100000/24/60/60)
    // Use GetMilliSpan to correct for rollover
    struct timeb tb;
    int nCount;
    ftime( &tb );
    nCount = (int) (tb.millitm + (tb.time & 0xfffff) * 1000);
    return nCount;
}

#else

static int BMK_GetMilliStart()
{
    // Based on newer gettimeofday()
    // Use GetMilliSpan to correct for rollover
    struct timeval tv;
    int nCount;
    gettimeofday(&tv, NULL);
    nCount = (int) (tv.tv_usec/1000 + (tv.tv_sec & 0xfffff) * 1000);
    return nCount;
}

#endif


static int BMK_GetMilliSpan( int nTimeStart )
{
    int nSpan = BMK_GetMilliStart() - nTimeStart;
    if ( nSpan < 0 )
        nSpan += 0x100000 * 1000;
    return nSpan;
}


static size_t BMK_findMaxMem(U64 requiredMem)
{
    size_t step = (64 MB);
    BYTE* testmem=NULL;

    requiredMem = (((requiredMem >> 26) + 1) << 26);
    requiredMem += 2*step;
    if (requiredMem > MAX_MEM) requiredMem = MAX_MEM;

    while (!testmem)
    {
        requiredMem -= step;
        testmem = (BYTE*) malloc ((size_t)requiredMem);
    }

    free (testmem);
    return (size_t) (requiredMem - step);
}


static U64 BMK_GetFileSize(char* infilename)
{
    int r;
#if defined(_MSC_VER)
    struct _stat64 statbuf;
    r = _stat64(infilename, &statbuf);
#else
    struct stat statbuf;
    r = stat(infilename, &statbuf);
#endif
    if (r || !S_ISREG(statbuf.st_mode)) return 0;   // No good...
    return (U64)statbuf.st_size;
}


//*********************************************************
//  Public function
//*********************************************************

void BMK_benchMemU16(chunkParameters_t* chunkP, int nbChunks, char* inFileName, int benchedSize,
                  U64* totalCompressedSize, double* totalCompressionTime, double* totalDecompressionTime,
                  int memLog)
{
    int loopNb, chunkNb;
    size_t cSize=0;
    double fastestC = 100000000., fastestD = 100000000.;
    double ratio=0.;
    U32 crcCheck=0;
    U32 crcOrig;

    // Init
    crcOrig = XXH32(chunkP[0].origBuffer, benchedSize,0);

    DISPLAY("\r%79s\r", "");
    for (loopNb = 1; loopNb <= nbIterations; loopNb++)
    {
        int nbLoops;
        int milliTime;

        // Compression
        DISPLAY("%1i-%-14.14s : %9i ->\r", loopNb, inFileName, benchedSize);
        { int i; for (i=0; i<benchedSize; i++) chunkP[0].compressedBuffer[i]=(char)i; }     // warmimg up memory

        nbLoops = 0;
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliStart() == milliTime);
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliSpan(milliTime) < TIMELOOP)
        {
            for (chunkNb=0; chunkNb<nbChunks; chunkNb++)
                chunkP[chunkNb].compressedSize = FSED_compressU16(chunkP[chunkNb].compressedBuffer, (const U16*)(chunkP[chunkNb].origBuffer), chunkP[chunkNb].origSize/2, memLog);
            nbLoops++;
        }
        milliTime = BMK_GetMilliSpan(milliTime);

        if ((double)milliTime < fastestC*nbLoops) fastestC = (double)milliTime/nbLoops;
        cSize=0; for (chunkNb=0; chunkNb<nbChunks; chunkNb++) cSize += chunkP[chunkNb].compressedSize;
        ratio = (double)cSize/(double)benchedSize*100.;

        DISPLAY("%1i-%-14.14s : %9i -> %9i (%5.2f%%),%7.1f MB/s\r", loopNb, inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000.);

        //DISPLAY("\n"); continue;   // skip decompression
        // Decompression
        //{ size_t i; for (i=0; i<benchedSize; i++) orig_buff[i]=0; }     // zeroing area, for CRC checking

        nbLoops = 0;
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliStart() == milliTime);
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliSpan(milliTime) < TIMELOOP)
        {
            for (chunkNb=0; chunkNb<nbChunks; chunkNb++)
                chunkP[chunkNb].compressedSize = FSED_decompressU16((unsigned short*)chunkP[chunkNb].origBuffer, chunkP[chunkNb].origSize/2, chunkP[chunkNb].compressedBuffer);
            nbLoops++;
        }
        milliTime = BMK_GetMilliSpan(milliTime);

        if ((double)milliTime < fastestD*nbLoops) fastestD = (double)milliTime/nbLoops;
        DISPLAY("%1i-%-14.14s : %9i -> %9i (%5.2f%%),%7.1f MB/s ,%7.1f MB/s\r", loopNb, inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000., (double)benchedSize / fastestD / 1000.);

        // CRC Checking
        crcCheck = XXH32(chunkP[0].origBuffer, benchedSize,0);
        if (crcOrig!=crcCheck) { DISPLAY("\n!!! WARNING !!! %14s : Invalid Checksum : %x != %x\n", inFileName, (unsigned)crcOrig, (unsigned)crcCheck); break; }
    }

    if (crcOrig==crcCheck)
    {
        if (ratio<100.)
            DISPLAY("%-16.16s : %9i -> %9i (%5.2f%%),%7.1f MB/s ,%7.1f MB/s\n", inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000., (double)benchedSize / fastestD / 1000.);
        else
            DISPLAY("%-16.16s : %9i -> %9i (%5.1f%%),%7.1f MB/s ,%7.1f MB/s \n", inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000., (double)benchedSize / fastestD / 1000.);
    }
    *totalCompressedSize    += cSize;
    *totalCompressionTime   += fastestC;
    *totalDecompressionTime += fastestD;
}


void BMK_benchMem(chunkParameters_t* chunkP, int nbChunks, char* inFileName, int benchedSize,
                  U64* totalCompressedSize, double* totalCompressionTime, double* totalDecompressionTime,
                  int nbSymbols, int memLog)
{
    int loopNb, chunkNb;
    size_t cSize=0;
    double fastestC = 100000000., fastestD = 100000000.;
    double ratio=0.;
    U32 crcCheck=0;
    U32 crcOrig;
    FSE_compress2_param_t param;

    // Init
    if (nbSymbols==0) { BMK_benchMemU16(chunkP, nbChunks, inFileName, benchedSize, totalCompressedSize, totalCompressionTime, totalDecompressionTime, memLog); return; }
    param.nbSymbols = nbSymbols;
    param.memLog = memLog;
    crcOrig = XXH32(chunkP[0].origBuffer, benchedSize,0);

    DISPLAY("\r%79s\r", "");
    for (loopNb = 1; loopNb <= nbIterations; loopNb++)
    {
        int nbLoops;
        int milliTime;

        // Compression
        DISPLAY("%1i-%-14.14s : %9i ->\r", loopNb, inFileName, benchedSize);
        { int i; for (i=0; i<benchedSize; i++) chunkP[0].compressedBuffer[i]=(char)i; }     // warmimg up memory

        nbLoops = 0;
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliStart() == milliTime);
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliSpan(milliTime) < TIMELOOP)
        {
            for (chunkNb=0; chunkNb<nbChunks; chunkNb++)
                //chunkP[chunkNb].compressedSize = FSE_compress_Nsymbols(chunkP[chunkNb].compressedBuffer, chunkP[chunkNb].origBuffer, chunkP[chunkNb].origSize, nbSymbols);
                chunkP[chunkNb].compressedSize = FSE_compress2(chunkP[chunkNb].compressedBuffer, chunkP[chunkNb].origBuffer, chunkP[chunkNb].origSize, param);
            nbLoops++;
        }
        milliTime = BMK_GetMilliSpan(milliTime);

        if ((double)milliTime < fastestC*nbLoops) fastestC = (double)milliTime/nbLoops;
        cSize=0; for (chunkNb=0; chunkNb<nbChunks; chunkNb++) cSize += chunkP[chunkNb].compressedSize;
        ratio = (double)cSize/(double)benchedSize*100.;

        DISPLAY("%1i-%-14.14s : %9i -> %9i (%5.2f%%),%7.1f MB/s\r", loopNb, inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000.);

        //DISPLAY("\n"); continue;   // skip decompression
        // Decompression
        //{ size_t i; for (i=0; i<benchedSize; i++) orig_buff[i]=0; }     // zeroing area, for CRC checking

        nbLoops = 0;
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliStart() == milliTime);
        milliTime = BMK_GetMilliStart();
        while(BMK_GetMilliSpan(milliTime) < TIMELOOP)
        {
            for (chunkNb=0; chunkNb<nbChunks; chunkNb++)
                chunkP[chunkNb].compressedSize = FSE_decompress(chunkP[chunkNb].origBuffer, chunkP[chunkNb].origSize, chunkP[chunkNb].compressedBuffer);
            nbLoops++;
        }
        milliTime = BMK_GetMilliSpan(milliTime);

        if ((double)milliTime < fastestD*nbLoops) fastestD = (double)milliTime/nbLoops;
        DISPLAY("%1i-%-14.14s : %9i -> %9i (%5.2f%%),%7.1f MB/s ,%7.1f MB/s\r", loopNb, inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000., (double)benchedSize / fastestD / 1000.);

        // CRC Checking
        crcCheck = XXH32(chunkP[0].origBuffer, benchedSize,0);
        if (crcOrig!=crcCheck) { DISPLAY("\n!!! WARNING !!! %14s : Invalid Checksum : %x != %x\n", inFileName, (unsigned)crcOrig, (unsigned)crcCheck); break; }
    }

    if (crcOrig==crcCheck)
    {
        if (ratio<100.)
            DISPLAY("%-16.16s : %9i -> %9i (%5.2f%%),%7.1f MB/s ,%7.1f MB/s\n", inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000., (double)benchedSize / fastestD / 1000.);
        else
            DISPLAY("%-16.16s : %9i -> %9i (%5.1f%%),%7.1f MB/s ,%7.1f MB/s \n", inFileName, (int)benchedSize, (int)cSize, ratio, (double)benchedSize / fastestC / 1000., (double)benchedSize / fastestD / 1000.);
    }
    *totalCompressedSize    += cSize;
    *totalCompressionTime   += fastestC;
    *totalDecompressionTime += fastestD;
}


int BMK_benchFiles(char** fileNamesTable, int nbFiles)
{
    int fileIdx=0;
    char* orig_buff;

    U64 totals = 0;
    U64 totalz = 0;
    double totalc = 0.;
    double totald = 0.;


    // Loop for each file
    while (fileIdx<nbFiles)
    {
        FILE*  inFile;
        char*  inFileName;
        U64    inFileSize;
        size_t benchedSize;
        int nbChunks;
        int maxCompressedChunkSize;
        size_t readSize;
        char* compressedBuffer; int compressedBuffSize;
        chunkParameters_t* chunkP;

        // Check file existence
        inFileName = fileNamesTable[fileIdx++];
        inFile = fopen( inFileName, "rb" );
        if (inFile==NULL) { DISPLAY( "Pb opening %s\n", inFileName); return 11; }

        // Check if file is fse compressed
        if (strstr(inFileName,".fse"))
        {
            char ch;
            DISPLAY("%s is compressed. Do you want to uncompress it (Y/N) : ", inFileName);
            ch = (char)getchar();
            if ((ch=='Y') || (ch=='y'))
            {
                size_t l = strlen(inFileName);
                char* destName = (char*)calloc(l,1);
                memcpy(destName, inFileName, l-4);
                FIO_overwriteMode();
                decompress_file (destName, inFileName);
                free(destName);
                fclose(inFile);
                inFileName[l-4]=0;
                inFile = fopen( inFileName, "rb" );
            }
        }

        // Memory allocation & restrictions
        inFileSize = BMK_GetFileSize(inFileName);
        benchedSize = (size_t) BMK_findMaxMem(inFileSize * 2) / 2;
        if ((U64)benchedSize > inFileSize) benchedSize = (size_t)inFileSize;
        if (benchedSize < inFileSize) DISPLAY("Not enough memory for '%s' full size; testing %i MB only...\n", inFileName, (int)(benchedSize>>20));

        // Alloc
        chunkP = (chunkParameters_t*) malloc(((benchedSize / chunkSize)+1) * sizeof(chunkParameters_t));
        orig_buff = (char*)malloc((size_t )benchedSize);
        nbChunks = (int) (benchedSize / chunkSize) + 1;
        maxCompressedChunkSize = FSE_compressBound(chunkSize);
        compressedBuffSize = nbChunks * maxCompressedChunkSize;
        compressedBuffer = (char*)malloc((size_t )compressedBuffSize);


        if (!orig_buff || !compressedBuffer)
        {
            DISPLAY("\nError: not enough memory!\n");
            free(orig_buff);
            free(compressedBuffer);
            free(chunkP);
            fclose(inFile);
            return 12;
        }

        // Init chunks data
        {
            int i;
            size_t remaining = benchedSize;
            char* in = orig_buff;
            char* out = compressedBuffer;
            for (i=0; i<nbChunks; i++)
            {
                chunkP[i].id = i;
                chunkP[i].origBuffer = in; in += chunkSize;
                if ((int)remaining > chunkSize) { chunkP[i].origSize = chunkSize; remaining -= chunkSize; } else { chunkP[i].origSize = (int)remaining; remaining = 0; }
                chunkP[i].compressedBuffer = out; out += maxCompressedChunkSize;
                chunkP[i].compressedSize = 0;
            }
        }

        // Fill input buffer
        DISPLAY("Loading %s...       \r", inFileName);
        readSize = fread(orig_buff, 1, benchedSize, inFile);
        fclose(inFile);

        if (readSize != benchedSize)
        {
            DISPLAY("\nError: problem reading file '%s' (%i read, should be %i) !!    \n", inFileName, (int)readSize, (int)benchedSize);
            free(orig_buff);
            free(compressedBuffer);
            free(chunkP);
            return 13;
        }

        // Bench
        BMK_benchMem(chunkP, nbChunks, inFileName, (int)benchedSize, &totalz, &totalc, &totald, 256, 0);
        totals += benchedSize;

        free(orig_buff);
        free(compressedBuffer);
        free(chunkP);
    }

    if (nbFiles > 1)
        DISPLAY("%-16.16s :%10llu ->%10llu (%5.2f%%), %6.1f MB/s , %6.1f MB/s\n", "  TOTAL", (long long unsigned int)totals, (long long unsigned int)totalz, (double)totalz/(double)totals*100., (double)totals/totalc/1000., (double)totals/totald/1000.);

    if (BMK_pause) { DISPLAY("press enter...\n"); getchar(); }

    return 0;
}



#define BLOCKRATIO 16
int BMK_benchFilesLZ4E(char** fileNamesTable, int nbFiles, int algoNb)
{
    int fileIdx=0;
    size_t blockSize = chunkSize * BLOCKRATIO;
    U64 totals = 0;
    U64 totalz = 0;
    double totalc = 0.;
    double totald = 0.;


    // Init

    // Loop for each file
    while (fileIdx<nbFiles)
    {
        FILE*  inFile;
        char*  inFileName;
        U64    inFileSize;
        size_t benchedSize;
        int nbChunks;
        size_t readSize;
        char* orig_buff;
        char* digest_buff;
        char* compressedBuffer; int compressedBuffSize;
        chunkParameters_t* chunkP;
        size_t digestedSize;
        int eType;

        // Check file existence
        inFileName = fileNamesTable[fileIdx++];
        inFile = fopen( inFileName, "rb" );
        if (inFile==NULL) { DISPLAY( "Pb opening %s\n", inFileName); exit(21); }

        // Memory allocation & restrictions
        inFileSize = BMK_GetFileSize(inFileName);
        benchedSize = (size_t) BMK_findMaxMem(inFileSize * 3) / 3;
        if ((U64)benchedSize > inFileSize) benchedSize = (size_t)inFileSize;
        if (benchedSize < inFileSize)
        {
            DISPLAY("Not enough memory for '%s' full size; testing %i MB only...\n", inFileName, (int)(benchedSize>>20));
        }

        // Alloc
        chunkP = (chunkParameters_t*) malloc(((benchedSize / blockSize)+1) * sizeof(chunkParameters_t));
        orig_buff = (char*)malloc(benchedSize);
        digest_buff = (char*)malloc(benchedSize);
        nbChunks = (int) (benchedSize / blockSize) + 1;
        compressedBuffSize = nbChunks * FSE_compressBound((int)blockSize);
        compressedBuffer = (char*)malloc((size_t)compressedBuffSize);


        if (!orig_buff || !compressedBuffer || !digest_buff)
        {
            DISPLAY("\nError: not enough memory!\n");
            free(orig_buff);
            free(digest_buff);
            free(compressedBuffer);
            free(chunkP);
            fclose(inFile);
            return 12;
        }

        // Fill orig buffer
        DISPLAY("Loading %s...       \r", inFileName);
        readSize = fread(orig_buff, 1, benchedSize, inFile);
        fclose(inFile);

        if (readSize != benchedSize)
        {
            free(orig_buff); free(compressedBuffer); free(chunkP);
            DISPLAY("\nError: problem reading file '%s' !!    \n", inFileName);
            exit(13);
        }

        for (eType=0; eType<(int)et_final; eType++)
        {
            // skip non selected extractions
            if ((algoNb>=0) && (eType!=algoNb)) continue;

            // Digest chunks data
            {
                int i;
                int remaining = (int)benchedSize;
                char* src = orig_buff;
                char* in  = digest_buff;
                char* out = compressedBuffer;
                blockSize = chunkSize*BLOCKRATIO;
                for (i=0; i<nbChunks; i++)
                {
                    int extractSize;
                    if (remaining==0) { nbChunks = i; break; }
                    chunkP[i].id = i;
                    chunkP[i].origBuffer = in;
                    remaining -= (int)blockSize; if (remaining < 0) { blockSize += remaining; remaining = 0; }
                    extractSize = LZ4_extractHC(src, in, (int)blockSize, (extractionType)eType);
                    src += blockSize;
                    in += extractSize;
                    chunkP[i].origSize = extractSize;
                    chunkP[i].compressedBuffer = out; out += FSE_compressBound(extractSize);
                    chunkP[i].compressedSize = 0;
                }
                digestedSize = in - digest_buff;
            }

            // Bench
            {
                int nbSymbols=256;
                int memLog=0;
                char localName[50] = {0};
                switch(eType)
                {
                case et_runLength:   strcat(localName, "rl."); nbSymbols= 16; memLog=12; break;
                case et_matchLength: strcat(localName, "ml."); nbSymbols= 16; memLog=12; break;
                case et_offset:      strcat(localName, "of."); nbSymbols= 16; memLog=12; break;
                case et_lastbits:    strcat(localName, "lb."); nbSymbols= 16; memLog=12; break;
                case et_literals:    strcat(localName, "lit.");nbSymbols=256; memLog=12; break;
                case et_runLengthU16:strcat(localName, "r16"); nbSymbols=  0; memLog= 9; break;
                case et_matchLengthU16:strcat(localName,"m16");nbSymbols=  0; memLog= 9; break;
                case et_offsetU16:   strcat(localName, "o16"); nbSymbols=  0; memLog=10; break;
                case et_offsetHigh:  strcat(localName, "ofh"); nbSymbols=256; memLog=11; break;
                }
                strcat(localName, inFileName);
                BMK_benchMem(chunkP, nbChunks, localName, (int)digestedSize, &totalz, &totalc, &totald, nbSymbols, memLog);
                totals += digestedSize;
            }

        }

        free(orig_buff);
        free(digest_buff);
        free(compressedBuffer);
        free(chunkP);
    }

    if (nbFiles > 1)
        DISPLAY("%-16.16s :%10llu ->%10llu (%5.2f%%), %6.1f MB/s , %6.1f MB/s\n", "  TOTAL", (long long unsigned int)totals, (long long unsigned int)totalz, (double)totalz/(double)totals*100., (double)totals/totalc/1000., (double)totals/totald/1000.);

    if (BMK_pause) { DISPLAY("press enter...\n"); getchar(); }

    return 0;
}


