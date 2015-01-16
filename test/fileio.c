/*
  fileio.c - simple generic file i/o handler
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
/*
  Note : this is stand-alone program.
  It is not part of FSE compression library, it is a user program of the FSE library.
  The license of FSE library is BSD.
  The license of this library is GPLv2.
*/

/**************************************
*  Compiler Options
**************************************/
/* Disable some Visual warning messages */
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_DEPRECATE     /* VS2005 */
#  pragma warning(disable : 4127)      /* disable: C4127: conditional expression is constant */
#endif

#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

#define _FILE_OFFSET_BITS 64   /* Large file support on 32-bits unix */
#define _POSIX_SOURCE 1        /* enable fileno() within <stdio.h> on unix */


/**************************************
*  Includes
**************************************/
#include <stdio.h>    /* fprintf, fopen, fread, _fileno, stdin, stdout */
#include <stdlib.h>   /* malloc, free */
#include <string.h>   /* strcmp, strlen */
#include <time.h>     /* clock */
#include "fileio.h"
#include "fse.h"
#include "xxhash.h"


/**************************************
*  OS-specific Includes
**************************************/
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(_WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>    // _O_BINARY
#  include <io.h>       // _setmode, _isatty
#  ifdef __MINGW32__
   int _fileno(FILE *stream);   // MINGW somehow forgets to include this windows declaration into <stdio.h>
#  endif
#  define SET_BINARY_MODE(file) _setmode(_fileno(file), _O_BINARY)
#  define IS_CONSOLE(stdStream) _isatty(_fileno(stdStream))
#else
#  include <unistd.h>   // isatty
#  define SET_BINARY_MODE(file)
#  define IS_CONSOLE(stdStream) isatty(fileno(stdStream))
#endif


/**************************************
*  Basic Types
**************************************/
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
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


/**************************************
*  Constants
**************************************/
#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

#define _1BIT  0x01
#define _2BITS 0x03
#define _3BITS 0x07
#define _4BITS 0x0F
#define _6BITS 0x3F
#define _8BITS 0xFF

#define BIT6  0x40
#define BIT7  0x80

static const unsigned FIO_magicNumber = 0x183E2308;
static const unsigned FIO_maxBlockSizeID = 0xB;   /* => 2MB block */
static const unsigned FIO_blockHeaderSize = 3;

#define FIO_FRAMEHEADERSIZE 5        /* as a define, because needed to allocated table on stack */
#define FIO_BLOCKSIZEID_DEFAULT  5   /* as a define, because needed to init static g_blockSizeId */
#define FSE_CHECKSUM_SEED        0

#define CACHELINE 64


/**************************************
*  Complex types
**************************************/
typedef enum { bt_compressed, bt_raw, bt_rle, bt_crc } bType_t;


/**************************************
*  Memory operations
**************************************/
static void FIO_writeLE32(void* memPtr, U32 val32)
{
    BYTE* p = memPtr;
    p[0] = (BYTE)val32;
    p[1] = (BYTE)(val32>>8);
    p[2] = (BYTE)(val32>>16);
    p[3] = (BYTE)(val32>>24);
}

static U32 FIO_readLE32(const void* memPtr)
{
    const BYTE* p = memPtr;
    return (U32)((U32)p[0] + ((U32)p[1]<<8) + ((U32)p[2]<<16) + ((U32)p[3]<<24));
}


/**************************************
*  Macros
**************************************/
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (g_displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static U32 g_displayLevel = 2;   /* 0 : no display;   1: errors;   2 : + result + interaction + warnings;   3 : + progression;   4 : + information */

#define DISPLAYUPDATE(l, ...) if (g_displayLevel>=l) { \
            if ((FIO_GetMilliSpan(g_time) > refreshRate) || (g_displayLevel>=4)) \
            { g_time = clock(); DISPLAY(__VA_ARGS__); \
            if (g_displayLevel>=4) fflush(stdout); } }
static const unsigned refreshRate = 150;
static clock_t g_time = 0;


/**************************************
*  Local Parameters
**************************************/
static U32 g_overwrite = 0;
static U32 g_blockSizeId = FIO_BLOCKSIZEID_DEFAULT;

void FIO_overwriteMode(void) { g_overwrite=1; }


/**************************************
*  Exceptions
**************************************/
#define DEBUG 0
#define DEBUGOUTPUT(...) if (DEBUG) DISPLAY(__VA_ARGS__);
#define EXM_THROW(error, ...)                                             \
{                                                                         \
    DEBUGOUTPUT("Error defined at %s, line %i : \n", __FILE__, __LINE__); \
    DISPLAYLEVEL(1, "Error %i : ", error);                                \
    DISPLAYLEVEL(1, __VA_ARGS__);                                         \
    DISPLAYLEVEL(1, "\n");                                                \
    exit(error);                                                          \
}


/**************************************
*  Version modifiers
**************************************/
#define DEFAULT_COMPRESSOR    FSE_compress
#define DEFAULT_DECOMPRESSOR  FSE_decompress


/**************************************
*  Functions
**************************************/
static unsigned FIO_GetMilliSpan(clock_t nPrevious)
{
    clock_t nCurrent = clock();
    unsigned nSpan = (unsigned)(((nCurrent - nPrevious) * 1000) / CLOCKS_PER_SEC);
    return nSpan;
}

static int FIO_GetBlockSize_FromBlockId   (int id) { return (1 << id) KB; }


static void get_fileHandle(const char* input_filename, const char* output_filename, FILE** pfinput, FILE** pfoutput)
{
    if (!strcmp (input_filename, stdinmark))
    {
        DISPLAYLEVEL(4,"Using stdin for input\n");
        *pfinput = stdin;
        SET_BINARY_MODE(stdin);
    }
    else
    {
        *pfinput = fopen(input_filename, "rb");
    }

    if (!strcmp (output_filename, stdoutmark))
    {
        DISPLAYLEVEL(4,"Using stdout for output\n");
        *pfoutput = stdout;
        SET_BINARY_MODE(stdout);
    }
    else
    {
        /* Check if destination file already exists */
        *pfoutput=0;
        if (strcmp(output_filename,nulmark)) *pfoutput = fopen( output_filename, "rb" );
        if (*pfoutput!=0)
        {
            fclose(*pfoutput);
            if (!g_overwrite)
            {
                char ch;
                if (g_displayLevel <= 1)   /* No interaction possible */
                    EXM_THROW(11, "Operation aborted : %s already exists", output_filename);
                DISPLAYLEVEL(2, "Warning : %s already exists\n", output_filename);
                DISPLAYLEVEL(2, "Overwrite ? (Y/N) : ");
                ch = (char)getchar();
                if ((ch!='Y') && (ch!='y')) EXM_THROW(11, "Operation aborted : %s already exists", output_filename);
            }
        }
        *pfoutput = fopen( output_filename, "wb" );
    }

    if ( *pfinput==0 ) EXM_THROW(12, "Pb opening %s", input_filename);
    if ( *pfoutput==0) EXM_THROW(13, "Pb opening %s", output_filename);
}


/*
Compressed format :
MAGICNUMBER - STREAMDESCRIPTOR - BLOCKHEADER - COMPRESSEDBLOCK - STREAMCRC
MAGICNUMBER - 4 bytes value, 0x183E2308, big endian
STREAMDESCRIPTOR
    1 byte value :
    bits 0-3 : max block size, 2^value from 0 to 0xF, with 5=>32 KB (min 0=>1KB, max 0xA=>1MB)
    bits 4-7 = 0 : reserved;
BLOCKHEADER
    3 bytes value :
    bits 6-7 : blockType (compressed, raw, rle, crc (end of Frame)
    rest : big endian : size of compressed block. (note : max is 2^21 == 2 MB) (or original size if rle)
COMPRESSEDBLOCK
    the compressed data itself.
STREAMCRC
    22 bits (xxh32() >> 5) checksum of the original data.
*/
unsigned long long FIO_compressFilename(const char* output_filename, const char* input_filename)
{
    U64 filesize = 0;
    U64 compressedfilesize = 0;
    char* in_buff;
    char* out_buff;
    FILE* finput;
    FILE* foutput;
    size_t sizeCheck;
    size_t inputBlockSize  = FIO_GetBlockSize_FromBlockId(g_blockSizeId);
    XXH32_state_t xxhState;


    /* Init */
    XXH32_reset (&xxhState, FSE_CHECKSUM_SEED);
    get_fileHandle(input_filename, output_filename, &finput, &foutput);

    /* Allocate Memory */
    in_buff  = (char*)malloc(inputBlockSize);
    out_buff = (char*)malloc(FSE_compressBound(inputBlockSize) + FIO_blockHeaderSize);
    if (!in_buff || !out_buff) EXM_THROW(21, "Allocation error : not enough memory");

    /* Write Frame Header */
    FIO_writeLE32(out_buff, FIO_magicNumber);
    out_buff[4] = (char)g_blockSizeId;          /* Max Block Size descriptor */
    sizeCheck = fwrite(out_buff, 1, FIO_FRAMEHEADERSIZE, foutput);
    if (sizeCheck!=FIO_FRAMEHEADERSIZE) EXM_THROW(22, "Write error : cannot write header");
    compressedfilesize += FIO_FRAMEHEADERSIZE;

    /* Main compression loop */
    while (1)
    {
        /* Fill input Buffer */
        size_t cSize;
        size_t inSize = fread(in_buff, (size_t)1, (size_t)inputBlockSize, finput);
        if (inSize==0) break;
        filesize += inSize;
        XXH32_update(&xxhState, in_buff, inSize);
        DISPLAYUPDATE(2, "\rRead : %u MB   ", (U32)(filesize>>20));

        /* Compress Block */
        cSize = FSE_compress(out_buff + FIO_blockHeaderSize, FSE_compressBound(inputBlockSize), in_buff, inSize);
        if (FSE_isError(cSize)) EXM_THROW(23, "Compression error : %s ", FSE_getErrorName(cSize));

        /* Write cBlock */
        switch(cSize)
        {
        case 0: /* raw */
            out_buff[2] = (BYTE)inSize;
            out_buff[1] = (BYTE)(inSize >> 8);
            out_buff[0] = (BYTE)((inSize >> 16) + (bt_raw << 6));
            sizeCheck = fwrite(out_buff, 1, FIO_blockHeaderSize, foutput);
            if (sizeCheck!=(size_t)(FIO_blockHeaderSize)) EXM_THROW(24, "Write error : cannot write block header");
            sizeCheck = fwrite(in_buff, 1, inSize, foutput);
            if (sizeCheck!=(size_t)(inSize)) EXM_THROW(25, "Write error : cannot write block");
            compressedfilesize += inSize + FIO_blockHeaderSize;
            break;
        case 1: /* rle */
            out_buff[2] = (BYTE)inSize;
            out_buff[1] = (BYTE)(inSize >> 8);
            out_buff[0] = (BYTE)((inSize >> 16) + (bt_rle << 6));
            out_buff[3] = in_buff[0];
            sizeCheck = fwrite(out_buff, 1, FIO_blockHeaderSize+1, foutput);
            if (sizeCheck!=(size_t)(FIO_blockHeaderSize+1)) EXM_THROW(26, "Write error : cannot write rle block");
            compressedfilesize += FIO_blockHeaderSize + 1;
            break;
        default : /* compressed */
            out_buff[2] = (BYTE)cSize;
            out_buff[1] = (BYTE)(cSize >> 8);
            out_buff[0] = (BYTE)((cSize >> 16) + (bt_compressed << 6));
            sizeCheck = fwrite(out_buff, 1, FIO_blockHeaderSize+cSize, foutput);
            if (sizeCheck!=(size_t)(FIO_blockHeaderSize+cSize)) EXM_THROW(27, "Write error : cannot write rle block");
            compressedfilesize += FIO_blockHeaderSize + cSize;
            break;
        }

        DISPLAYUPDATE(2, "\rRead : %u MB  ==> %.2f%%   ", (U32)(filesize>>20), (double)compressedfilesize/filesize*100);
    }

    /* Checksum */
    {
        U32 checksum = XXH32_digest(&xxhState);
        checksum = (checksum >> 5) & ((1U<<22)-1);
        out_buff[2] = (BYTE)checksum;
        out_buff[1] = (BYTE)(checksum >> 8);
        out_buff[0] = (BYTE)((checksum >> 16) + (bt_crc << 6));
        sizeCheck = fwrite(out_buff, 1, FIO_blockHeaderSize, foutput);
        if (sizeCheck!=FIO_blockHeaderSize) EXM_THROW(28, "Write error : cannot write checksum");
        compressedfilesize += FIO_blockHeaderSize;
    }

    /* Status */
    DISPLAYLEVEL(2, "\r%79s\r", "");
    DISPLAYLEVEL(2,"Compressed %llu bytes into %llu bytes ==> %.2f%%\n",
        (unsigned long long) filesize, (unsigned long long) compressedfilesize, (double)compressedfilesize/filesize*100);

    /* clean */
    free(in_buff);
    free(out_buff);
    fclose(finput);
    fclose(foutput);

    return compressedfilesize;
}


/*
Compressed format :
MAGICNUMBER - STREAMDESCRIPTOR - BLOCKHEADER - COMPRESSEDBLOCK - STREAMCRC
MAGICNUMBER - 4 bytes value, 0x183E2308, big endian
STREAMDESCRIPTOR
    1 byte value :
    bits 0-3 : max block size, 2^value from 0 to 0xF, with 5=>32 KB (min 0=>1KB, max 0xA=>1MB)
    bits 4-7 = 0 : reserved;
BLOCKHEADER
    3 bytes value :
    bits 6-7 : blockType (compressed, raw, rle, crc (end of Frame)
    rest : big endian : size of compressed block. (note : max is 2^21 == 2 MB) (or original size if rle)
COMPRESSEDBLOCK
    the compressed data itself.
STREAMCRC
    22 bits (xxh32() >> 5) checksum of the original data.
*/
unsigned long long FIO_decompressFilename(const char* output_filename, const char* input_filename)
{
    FILE* finput, *foutput;
    U64   filesize = 0;
    U32   header32[(FIO_FRAMEHEADERSIZE+3) >> 2];
    BYTE* header = (BYTE*)header32;
    BYTE* in_buff;
    BYTE* out_buff;
    BYTE* ip;
    U32   blockSize;
    U32   blockSizeId;
    size_t sizeCheck;
    U32   magicNumber;
    U32*  magicNumberP = header32;
    size_t inputBufferSize;
    XXH32_state_t xxhState;


    /* Init */
    XXH32_reset(&xxhState, FSE_CHECKSUM_SEED);
    get_fileHandle(input_filename, output_filename, &finput, &foutput);

    /* check header */
    sizeCheck = fread(header, (size_t)1, FIO_FRAMEHEADERSIZE, finput);
    if (sizeCheck != FIO_FRAMEHEADERSIZE) EXM_THROW(30, "Read error : cannot read header\n");

    magicNumber = FIO_readLE32(magicNumberP);
    if (magicNumber != FIO_magicNumber) EXM_THROW(31, "Wrong file type : unknown header\n");
    blockSizeId = header[4];
    if (blockSizeId > FIO_maxBlockSizeID) EXM_THROW(32, "Wrong version : unknown header flags\n");
    blockSize = FIO_GetBlockSize_FromBlockId(blockSizeId);

    /* Allocate Memory */
    inputBufferSize = blockSize + FIO_blockHeaderSize;
    in_buff  = malloc(inputBufferSize);
    out_buff = malloc(blockSize);
    if (!in_buff || !out_buff) EXM_THROW(33, "Allocation error : not enough memory");
    ip = in_buff;

    /* read first bHeader */
    sizeCheck = fread(in_buff, 1, FIO_blockHeaderSize, finput);
    if (sizeCheck != FIO_blockHeaderSize) EXM_THROW(34, "Read error : cannot read header\n");

    /* Main Loop */
    while (1)
    {
        size_t toReadSize, readSize, bType, rSize=0, cSize;

        /* Decode header */
        bType = (ip[0] & (BIT7+BIT6)) >> 6;
        if (bType == bt_crc) break;   /* end - frame content CRC */
        switch(bType)
        {
          case bt_compressed :
          case bt_raw :
            cSize = ip[2] + (ip[1]<<8) + ((ip[0] & _6BITS) << 16);
            break;
          case bt_rle :
            cSize = 1;
            rSize = ip[2] + (ip[1]<<8) + ((ip[0] & _6BITS) << 16);
            break;
          default :
            EXM_THROW(35, "unknown block header");   /* should not happen */
        }

        /* Fill input buffer */
        toReadSize = cSize + FIO_blockHeaderSize;
        readSize = fread(in_buff, 1, toReadSize, finput);
        if (readSize != toReadSize) EXM_THROW(36, "Read error");
        ip = in_buff + cSize;

        /* Decode block */
        switch(bType)
        {
          case bt_compressed :
            rSize = FSE_decompress(out_buff, blockSize, in_buff, cSize);
            if (FSE_isError(rSize)) EXM_THROW(37, "Decoding error : %s", FSE_getErrorName(rSize));
            break;
          case bt_raw :
            break;
          case bt_rle :
            memset(out_buff, in_buff[0], rSize);
            break;
          default :
            EXM_THROW(38, "unknown block header");   /* should not happen */
        }

        /* Write block */
        switch(bType)
        {
          size_t writeSizeCheck;

          case bt_compressed :
          case bt_rle :
            writeSizeCheck = fwrite(out_buff, 1, rSize, foutput);
            if (writeSizeCheck != rSize) EXM_THROW(39, "Write error : unable to write data block to destination file");
            XXH32_update(&xxhState, out_buff, rSize);
            filesize += rSize;
            break;
          case bt_raw :
            writeSizeCheck = fwrite(in_buff, 1, cSize, foutput);
            if (writeSizeCheck != cSize) EXM_THROW(40, "Write error : unable to write data block to destination file");
            XXH32_update(&xxhState, in_buff, cSize);
            filesize += cSize;
            break;
          default :
            EXM_THROW(41, "unknown block header");   /* should not happen */
        }
    }

    /* CRC verification */
    {
        U32 CRCsaved = ip[2] + (ip[1]<<8) + ((ip[0] & _6BITS) << 16);
        U32 CRCcalculated = (XXH32_digest(&xxhState) >> 5) & ((1U<<22)-1);
        if (CRCsaved != CRCcalculated) EXM_THROW(42, "CRC error : wrong checksum, corrupted data");
    }

    DISPLAYLEVEL(2, "\r%79s\r", "");
    DISPLAYLEVEL(2,"Decoded %llu bytes\n", (long long unsigned)filesize);

    /* clean */
    free(in_buff);
    free(out_buff);
    fclose(finput);
    fclose(foutput);

    return filesize;
}


