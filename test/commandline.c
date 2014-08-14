/*
  commandline.c - simple command line interface manager, for FSE
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
  The license of this program is GPLv2.
*/


//***************************************************
// Compiler instructions
//***************************************************
#define _CRT_SECURE_NO_WARNINGS   // Remove warning under visual studio
#define _FILE_OFFSET_BITS 64   // Large file support on 32-bits unix
#define _POSIX_SOURCE 1        // for fileno() within <stdio.h> on unix


//***************************************************
// Includes
//***************************************************
#include <stdlib.h>   // exit
#include <stdio.h>    // fprintf
#include <string.h>   // strcmp, strcat
#include "bench.h"
#include "fileio.h"
#include "lz4hce.h"   // et_final


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


//***************************************************
// Constants
//***************************************************
#define COMPRESSOR_NAME "FSE : Finite State Entropy"
#define AUTHOR "Yann Collet"
#define WELCOME_MESSAGE "%s, %i-bits demo by %s (%s)\n", COMPRESSOR_NAME, (int)sizeof(void*)*8, AUTHOR, __DATE__
#define FSE_EXTENSION ".fse"


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
static int   fse_pause = 0;


//***************************************************
// Functions
//***************************************************
static int usage(void)
{
    DISPLAY("Usage :\n");
    DISPLAY("%s [arg] inputFilename [-o [outputFilename]]\n", programName);
    DISPLAY("Arguments :\n");
    DISPLAY("(default): core loop timing tests\n");
    DISPLAY(" -b : benchmark full mode\n");
    DISPLAY(" -m : benchmark lowMem mode\n");
    DISPLAY(" -z : benchmark using zlib's huffman\n");
    DISPLAY(" -d : decompression (default for %s extension)\n", FSE_EXTENSION);
    DISPLAY(" -o : force compression\n");
    DISPLAY(" -i#: iteration loops [1-9](default : 4), benchmark mode only\n");
    DISPLAY(" -h/-H : display help/long help and exit\n");
    return 0;
}


static int badusage(void)
{
    DISPLAYLEVEL(1, "Incorrect parameters\n");
    if (displayLevel >= 1) usage();
    exit(1);
}


static void waitEnter(void)
{
    DISPLAY("Press enter to continue...\n");
    getchar();
}

int main(int argc, char** argv)
{
    int   i,
          forceCompress=0, decode=0, bench=3, benchLZ4e=0; // default action if no argument
    int   algoNb = -1;
    int   indexFileNames=0;
    char* input_filename=0;
    char* output_filename=0;
    int   nextNameIsOutput = 0;
    char  extension[] = FSE_EXTENSION;


    // Welcome message
    programName = argv[0];
    DISPLAY(WELCOME_MESSAGE);

    if (argc<1) badusage();

    for(i = 1; i <= argc; i++)
    {
        char* argument = argv[i];
        nextNameIsOutput --;

        if(!argument) continue;   // Protection if argument empty

        // Decode command (note : aggregated commands are allowed)
        if (argument[0]=='-')
        {
            // '-' means stdin/stdout
            if (argument[1]==0)
            {
                if (!input_filename) input_filename=stdinmark;
                else output_filename=stdoutmark;
            }

            while (argument[1]!=0)
            {
                argument ++;

                switch(argument[0])
                {
                    // Display help
                case 'V': DISPLAY(WELCOME_MESSAGE); return 0;   // Version
                case 'h':
                case 'H': usage(); return 0;

                    // compression
                case 'o': bench=0; nextNameIsOutput=2; break;

                    // Decoding
                case 'd': decode=1; bench=0; break;

                    // Benchmark full mode
                case 'b': bench=1; break;

                    // Benchmark full mode
                case 'm': DISPLAY("benchmark using experimental lowMem mode\n");
                    bench=1;
                    BMK_SetByteCompressor(2);
                    break;

                    // zlib Benchmark mode
                case 'z':
                    bench=1;
                    BMK_SetByteCompressor(3);
                    break;

                    // Benchmark LZ4 extracted fields (hidden)
                case 'l': benchLZ4e=1;
                    algoNb = 0;
                    while ((argument[1]>='0') && (argument[1]<='9')) { algoNb *= 10; algoNb += argument[1]-'0'; argument++; }
                    algoNb -= 1;
                    if (algoNb >= et_final) algoNb = et_final-1;
                    break;

                    // Test
                case 't': decode=1; output_filename=nulmark; break;

                    // Overwrite
                case 'f': FIO_overwriteMode(); break;

                    // Verbose mode
                case 'v': displayLevel=4; break;

                    // Quiet mode
                case 'q': displayLevel--; break;

                    // keep source file (default anyway, so useless) (for xz/lzma compatibility)
                case 'k': break;

                    // Modify Block Properties
                case 'B': break;   // to be completed later

                    // Modify Stream properties
                case 'S': break;   // to be completed later

                    // Modify Nb Iterations (benchmark only)
                case 'i':
                    if ((argument[1] >='1') && (argument[1] <='9'))
                    {
                        int iters = argument[1] - '0';
                        BMK_SetNbIterations(iters);
                        argument++;
                    }
                    break;

                    // Pause at the end (hidden option)
                case 'p': fse_pause=1; break;

                    // Change FSE table size (hidden option)
                case 'M':
                    if ((argument[1] >='1') && (argument[1] <='9'))
                    {
                        int tableLog = argument[1] - '0';
                        BMK_SetTableLog(tableLog);
                        argument++;
                    }
                    break;

                    // Unrecognised command
                default : badusage();
                }
            }
            continue;
        }

        // following -o argument
        if (nextNameIsOutput == 1) { output_filename=argument; continue; }

        // first provided filename is input
        if (!input_filename) { input_filename=argument; indexFileNames=i; continue; }
    }

    // End of command line reading
    DISPLAYLEVEL(3, WELCOME_MESSAGE);

    // No input filename ==> use stdin
    if(!input_filename) { input_filename=stdinmark; }

    // Check if input is defined as console; trigger an error in this case
    if (!strcmp(input_filename, stdinmark)  && IS_CONSOLE(stdin)                 ) badusage();

    // Check if benchmark is selected
    if (benchLZ4e) { BMK_benchFilesLZ4E(argv+indexFileNames, argc-indexFileNames, algoNb); goto _end; }

    // Check if benchmark is selected
    if (bench==1) { BMK_benchFiles(argv+indexFileNames, argc-indexFileNames); goto _end; }
    if (bench==3) { BMK_benchCore_Files(argv+indexFileNames, argc-indexFileNames); goto _end; }

    // No output filename ==> try to select one automatically (when possible)
    while (!output_filename)
    {
        if (!IS_CONSOLE(stdout)) { output_filename=stdoutmark; break; }   // Default to stdout whenever possible (i.e. not a console)
        if ((!decode) && !(forceCompress))   // auto-determine compression or decompression, based on file extension
        {
            size_t l = strlen(input_filename);
            if (!strcmp(input_filename+(l-4), FSE_EXTENSION)) decode=1;
        }
        if (!decode)   // compression to file
        {
            size_t l = strlen(input_filename);
            output_filename = (char*)calloc(1,l+5);
            strcpy(output_filename, input_filename);
            strcpy(output_filename+l, FSE_EXTENSION);
            DISPLAYLEVEL(2, "Compressed filename will be : %s \n", output_filename);
            break;
        }
        // decompression to file (automatic name will work only if input filename has correct format extension)
        {
            size_t outl;
            size_t inl = strlen(input_filename);
            output_filename = (char*)calloc(1,inl+1);
            strcpy(output_filename, input_filename);
            outl = inl;
            if (inl>4)
                while ((outl >= inl-4) && (input_filename[outl] ==  extension[outl-inl+4])) output_filename[outl--]=0;
            if (outl != inl-5) { DISPLAYLEVEL(1, "Cannot determine an output filename\n"); badusage(); }
            DISPLAYLEVEL(2, "Decoding into filename : %s \n", output_filename);
        }
    }

    // No warning message in pure pipe mode (stdin + stdout)
    if (!strcmp(input_filename, stdinmark) && !strcmp(output_filename,stdoutmark) && (displayLevel==2)) displayLevel=1;

    // Check if input or output are defined as console; trigger an error in this case
    if (!strcmp(input_filename, stdinmark)  && IS_CONSOLE(stdin)                 ) badusage();
    if (!strcmp(output_filename,stdoutmark) && IS_CONSOLE(stdout)                ) badusage();

    if (decode) decompress_file(output_filename, input_filename);
    else compress_file(output_filename, input_filename);

_end:
    if (fse_pause) waitEnter();
    return 0;
}
