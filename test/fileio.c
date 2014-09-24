/*
  fileio.c - simple generic file i/o handler
  Copyright (C) Yann Collet 2013-2014
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
  - Public forum : https://groups.google.com/forum/#!forum/lz4c
*/
/*
  Note : this is stand-alone program.
  It is not part of FSE compression library, it is a user program of the FSE library.
  The license of FSE library is BSD.
  The license of this library is GPLv2.
*/

//**************************************
// Tuning parameters
//**************************************


//**************************************
// Compiler Options
//**************************************
// Disable some Visual warning messages
#ifdef _MSC_VER  // Visual Studio
#  define _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_DEPRECATE     // VS2005
#  pragma warning(disable : 4127)      // disable: C4127: conditional expression is constant
#endif

#define _FILE_OFFSET_BITS 64   // Large file support on 32-bits unix
#define _POSIX_SOURCE 1        // for fileno() within <stdio.h> on unix


//****************************
// Includes
//****************************
#include <stdio.h>    // fprintf, fopen, fread, _fileno, stdin, stdout
#include <stdlib.h>   // malloc
#include <string.h>   // strcmp, strlen
#include "fileio.h"
#include "fse.h"
#include "xxhash.h"


//****************************
// OS-specific Includes
//****************************
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


//**************************************
// Compiler-specific functions
//**************************************
#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

#if defined(_MSC_VER)    // Visual Studio
#  define swap32 _byteswap_ulong
#elif GCC_VERSION >= 403
#  define swap32 __builtin_bswap32
#else
  static inline unsigned int swap32(unsigned int x)
  {
    return ((x << 24) & 0xff000000 ) |
           ((x <<  8) & 0x00ff0000 ) |
           ((x >>  8) & 0x0000ff00 ) |
           ((x >> 24) & 0x000000ff );
  }
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


//****************************
// Constants
//****************************
#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

#define _1BIT  0x01
#define _2BITS 0x03
#define _3BITS 0x07
#define _4BITS 0x0F
#define _8BITS 0xFF

#define MAGICNUMBER_SIZE   4
#define FSE_MAGIC_NUMBER   0x183E2307

#define CACHELINE 64
#define FSE_BLOCKSIZEID_DEFAULT  5
#define FSE_BUFFERSIZEID_DEFAULT 5
#define FSE_CHECKSUM_SEED        0


//**************************************
// Architecture Macros
//**************************************
static const int one = 1;
#define CPU_LITTLE_ENDIAN   (*(char*)(&one))
#define CPU_BIG_ENDIAN      (!CPU_LITTLE_ENDIAN)
#define LITTLE_ENDIAN_32(i) (CPU_LITTLE_ENDIAN?(i):swap32(i))


//**************************************
// Macros
//**************************************
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }


//**************************************
// Local Parameters
//**************************************
static int   displayLevel = 2;   // 0 : no display  // 1: errors  // 2 : + result + interaction + warnings ;  // 3 : + progression;  // 4 : + information
static int   overwrite = 0;
static int   globalBlockSizeId = FSE_BLOCKSIZEID_DEFAULT;
static int   bufferSizeId = FSE_BUFFERSIZEID_DEFAULT;


//**************************************
// Exceptions
//**************************************
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


//**************************************
// Version modifiers
//**************************************
#define DEFAULT_COMPRESSOR    FSE_compress
#define DEFAULT_DECOMPRESSOR  FSE_decompress


//**************************************
// Parameters
//**************************************
void FIO_overwriteMode(void) { overwrite=1; }


//****************************
// Functions
//****************************
static int          FIO_GetBlockSize_FromBlockId   (int id) { return (1 << id) KB; }
static int          FIO_GetBufferSize_FromBufferId (int id) { return (1 << (id + 5)) KB; }


int get_fileHandle(char* input_filename, char* output_filename, FILE** pfinput, FILE** pfoutput)
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
        // Check if destination file already exists
        *pfoutput=0;
        if (strcmp(output_filename,nulmark)) *pfoutput = fopen( output_filename, "rb" );
        if (*pfoutput!=0)
        {
            fclose(*pfoutput);
            if (!overwrite)
            {
                char ch;
                if (displayLevel <= 1) EXM_THROW(11, "Operation aborted : %s already exists", output_filename);   // No interaction possible
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

    return 0;
}


/*
Compression format :
MAGICNUMBER - STREAMDESCRIPTOR - MULTIBLOCKHEADER - (LASTBLOCKSIZE) - COMPRESSEDBLOCK - STREAMCRC
MAGICNUMBER - 4 bytes value, 0x183E2301, little endian
STREAMDESCRIPTOR
    1 byte value :
    bits 0-3 : block size, 2^value from 0 to 0xF, with 5=>32 KB (0=>1KB, 0xF=>32MB)
    bits 4-7 = 0 : reserved; All blocks must be full, except last one
MULTIBLOCKHEADER
    1 byte value :
    if 0 : next block is the last, (BLOCKSIZE) will be provided
    if >0 : the next n blocks are full ones
(LASTBLOCKSIZE)
    n bytes value :
    provided for last block only.
    gives the uncompressed size of last block; necessarily <= block size
    the number of bytes required depends on block size (ex : for 32KB blocks, n=2)
COMPRESSEDBLOCK
    the compressed data itself. Note that its size is not provided. Maximum size is Blocksize+1
STREAMCRC
    4 bytes xxh32() value of the original data.
*/
int compress_file(char* output_filename, char* input_filename)
{
    int (*compressionFunction)(void*, const unsigned char*, unsigned) = DEFAULT_COMPRESSOR;
    U64 filesize = 0;
    U64 compressedfilesize = 0;
    char* in_buff;
    char* out_buff;
    FILE* finput;
    FILE* foutput;
    size_t sizeCheck;
    size_t inputBlockSize  = FIO_GetBlockSize_FromBlockId(globalBlockSizeId);
    size_t inputBufferSize = FIO_GetBufferSize_FromBufferId(bufferSizeId);
    int nbBlocksPerBuffer;
    int lastBlockDone=0;
    void* hashCtx = XXH32_init(FSE_CHECKSUM_SEED);


    // Init
    get_fileHandle(input_filename, output_filename, &finput, &foutput);

    // Allocate Memory
    if (inputBufferSize < inputBlockSize) inputBufferSize = inputBlockSize;
    nbBlocksPerBuffer = (int)((inputBufferSize + (inputBlockSize-1)) / inputBlockSize);
    in_buff  = (char*)malloc(inputBufferSize);
    out_buff = (char*)malloc(nbBlocksPerBuffer * FSE_compressBound((int)inputBlockSize) + CACHELINE);
    if (!in_buff || !out_buff) EXM_THROW(21, "Allocation error : not enough memory");

    // Write Archive Header
    *(U32*)out_buff = LITTLE_ENDIAN_32(FSE_MAGIC_NUMBER);   // Magic Number
    out_buff[4] = (char)globalBlockSizeId;                  // Block Size descriptor
    sizeCheck = fwrite(out_buff, 1, MAGICNUMBER_SIZE+1, foutput);
    if (sizeCheck!=MAGICNUMBER_SIZE+1) EXM_THROW(22, "Write error : cannot write header");
    compressedfilesize += MAGICNUMBER_SIZE+1;

    // Main Loop
    while (1)
    {
        // Fill input Buffer
        int outSize;
        size_t inSize = fread(in_buff, (size_t)1, (size_t)inputBufferSize, finput);
        if ((inSize==0) && (lastBlockDone)) break;
        filesize += inSize;
        XXH32_update(hashCtx, in_buff, (int)inSize);
        DISPLAYLEVEL(3, "\rRead : %i MB   ", (int)(filesize>>20));

        // Compress Blocks
        {
            const char* ip = in_buff;
            char* op = out_buff+1;
            int nbFullBlocks = (int)(inSize / inputBlockSize);
            int i;
            *(BYTE*)out_buff = (BYTE)nbFullBlocks;
            for (i=0; i<nbFullBlocks; i++)
            {
                int errorCode = compressionFunction(op, (unsigned char*)ip, (int)inputBlockSize);
                if (errorCode==-1) EXM_THROW(22, "Compression error");
                op += errorCode;
                ip += inputBlockSize;
            }
            if (((nbFullBlocks * inputBlockSize) < inSize) || (!inSize))  // last Block
            {
                int errorCode;
                int nbBytes = ((globalBlockSizeId+10)/8) + 1;   // nb Bytes to describe last block size
                int lastBlockSize = (int)inSize & (inputBlockSize-1);
                if (nbFullBlocks) *op++= 0;               // Last block flag, useless if nbFullBlocks==0
                *(U32*)op = LITTLE_ENDIAN_32((U32)lastBlockSize); op+= nbBytes;
                errorCode = compressionFunction(op, (unsigned char*)ip, lastBlockSize);
                if (errorCode==-1) EXM_THROW(22, "Compression error, last block");
                op += errorCode;
                ip +=  lastBlockSize;
                lastBlockDone=1;
            }
            outSize = (int)(op - out_buff);
            compressedfilesize += outSize;
            DISPLAYLEVEL(3, "\rRead : %i MB  ==> %.2f%%   ", (int)(filesize>>20), (double)compressedfilesize/filesize*100);
        }

        // Write Block
        sizeCheck = fwrite(out_buff, 1, outSize, foutput);
        if (sizeCheck!=(size_t)(outSize)) EXM_THROW(23, "Write error : cannot write compressed block");
    }

    // Checksum
    *(U32*)out_buff = LITTLE_ENDIAN_32(XXH32_digest(hashCtx));
    compressedfilesize += 4;
    sizeCheck = fwrite(out_buff, 1, 4, foutput);
    if (sizeCheck!=4) EXM_THROW(24, "Write error : cannot write checksum");

    // Status
    DISPLAYLEVEL(2, "\r%79s\r", "");
    DISPLAYLEVEL(2,"Compressed %llu bytes into %llu bytes ==> %.2f%%\n",
        (unsigned long long) filesize, (unsigned long long) compressedfilesize, (double)compressedfilesize/filesize*100);

    // Close & Free
    free(in_buff);
    free(out_buff);
    fclose(finput);
    fclose(foutput);

    return 0;
}


#define HEADERSIZE 5
unsigned long long decompress_file(char* output_filename, char* input_filename)
{
    FILE* finput, *foutput;
    U64   filesize = 0;
    char  header[HEADERSIZE];
    char* in_buff;
    char* out_buff;
    char* ip;
    char* ifill;
    char* iend;
    U32   blockSize;
    int   blockSizeId;
    size_t sizeCheck;
    U32   magicNumber;
    U32*  magicNumberP = (U32*) header;
    size_t inputBufferSize;
    int nbFullBlocks = 0;
    void* hashCtx = XXH32_init(FSE_CHECKSUM_SEED);


    // Init
    get_fileHandle(input_filename, output_filename, &finput, &foutput);

    // Read and then check header
    sizeCheck = fread(header, (size_t)1, HEADERSIZE, finput);
    if (sizeCheck != HEADERSIZE) EXM_THROW(30, "Read error : cannot read header\n");

    magicNumber = LITTLE_ENDIAN_32(*magicNumberP);
    if (magicNumber != FSE_MAGIC_NUMBER) EXM_THROW(31, "Wrong file type : unrecognised header\n");
    blockSizeId = header[4];
    if (blockSizeId > 0xF) EXM_THROW(32, "Wrong version : unrecognised header flags\n");
    blockSize = FIO_GetBlockSize_FromBlockId(blockSizeId);

    // Allocate Memory
    inputBufferSize = FIO_GetBufferSize_FromBufferId(bufferSizeId);
    if (inputBufferSize < 2* blockSize) inputBufferSize = 2*blockSize;   // Minimum input buffer size
    in_buff  = (char*)malloc(inputBufferSize);
    out_buff = (char*)malloc(blockSize);
    if (!in_buff || !out_buff) EXM_THROW(33, "Allocation error : not enough memory");
    ip = in_buff;
    ifill = ip;
    iend = ip + inputBufferSize;

    // Main Loop
    while (1)
    {
        size_t toReadSize, readSize;

        // Fill input buffer
        toReadSize = iend-ifill;
        readSize = fread(ifill, 1, toReadSize, finput);
        if ((readSize != toReadSize) && ferror(finput)) EXM_THROW(34, "Read error");

        // Decode while enough data
        while ((size_t)(iend-ip) > (size_t)FSE_compressBound(blockSize))
        {
            size_t writeSizeCheck;
            int errorCode;
            if (nbFullBlocks == 0)
            {
                nbFullBlocks = *ip++;
                if (!nbFullBlocks) goto _lastBlock;   // goto last block
            }
            errorCode = FSE_decompress((unsigned char*)out_buff, blockSize, ip);
            if (errorCode == -1) EXM_THROW(33, "Decoding error : compressed data block corrupted");
            ip += errorCode;
            filesize += blockSize;
            nbFullBlocks--;

            writeSizeCheck = fwrite(out_buff, 1, blockSize, foutput);
            if (writeSizeCheck != blockSize) EXM_THROW(34, "Write error : unable to write data block to destination file");
            XXH32_update(hashCtx, out_buff, blockSize);
        }

        // move remaining data to beginning of buffer
        {
            size_t toCopy = iend-ip;
            memcpy(in_buff, ip, toCopy);
            ifill = in_buff + toCopy;
            ip = in_buff;
        }
    }

_lastBlock:
    {
        int errorCode;
        int nbBytes = ((blockSizeId+10)/8)+1;   // Nb Bytes to describe last block size
        U32 lastBlockSize = LITTLE_ENDIAN_32(*(U32*)ip);
        U32 mask;
        ip += nbBytes;
        switch(nbBytes)
        {
        case 2: mask = 0xFFFF; break;
        case 3: mask = 0xFFFFFF; break;
        default:
        case 4: mask = 0xFFFFFFFF;
        }
        lastBlockSize &= mask;

        errorCode = FSE_decompress((unsigned char*)out_buff, lastBlockSize, ip);
        if (errorCode == -1) EXM_THROW(33, "Decoding error : last block failed");
        ip += errorCode;
        filesize += lastBlockSize;

        sizeCheck = fwrite(out_buff, 1, lastBlockSize, foutput);
        if (sizeCheck != lastBlockSize) EXM_THROW(34, "Write error : unable to write data block to destination file");
        XXH32_update(hashCtx, out_buff, lastBlockSize);
    }

    // CRC verification
    {
        U32 CRCsaved = *(U32*)ip;
        U32 CRCcalculated = XXH32_digest(hashCtx);
        if (CRCsaved != CRCcalculated) EXM_THROW(35, "CRC error : wrong checksum, corrupted data");
    }

    DISPLAYLEVEL(2, "\r%79s\r", "");
    DISPLAYLEVEL(2,"Decoded %llu bytes\n", (long long unsigned)filesize);

    // Free
    free(in_buff);
    free(out_buff);
    fclose(finput);
    fclose(foutput);

    return filesize;
}


