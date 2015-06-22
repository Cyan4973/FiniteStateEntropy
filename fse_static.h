/* ******************************************************************
   FSE : Finite State Entropy coder
   header file for static linking (only)
   Copyright (C) 2013-2015, Yann Collet

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
   - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */
#pragma once

#if defined (__cplusplus)
extern "C" {
#endif


/******************************************
*  FSE API compatible with DLL
******************************************/
#include "fse.h"


/******************************************
*  Static allocation
******************************************/
#define FSE_MAX_HEADERSIZE 512
#define FSE_COMPRESSBOUND(size) (size + (size>>7) + FSE_MAX_HEADERSIZE)   /* Macro can be useful for static allocation */
/* You can statically allocate a CTable as a table of unsigned int using below macro */
#define FSE_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue)   (1 + (1<<(maxTableLog-1)) + ((maxSymbolValue+1)*2))
#define FSE_DTABLE_SIZE_U32(maxTableLog)                   (1 + (1<<maxTableLog))


/******************************************
*  Error Management
******************************************/
#define FSE_LIST_ERRORS(ITEM) \
        ITEM(FSE_OK_NoError) ITEM(FSE_ERROR_GENERIC) \
        ITEM(FSE_ERROR_tableLog_tooLarge) ITEM(FSE_ERROR_maxSymbolValue_tooLarge) \
        ITEM(FSE_ERROR_dstSize_tooSmall) ITEM(FSE_ERROR_srcSize_wrong)\
        ITEM(FSE_ERROR_corruptionDetected) \
        ITEM(FSE_ERROR_maxCode)

#define FSE_GENERATE_ENUM(ENUM) ENUM,
typedef enum { FSE_LIST_ERRORS(FSE_GENERATE_ENUM) } FSE_errorCodes;  /* enum is exposed, to detect & handle specific errors; compare function result to -enum value */


/******************************************
*  FSE advanced API
******************************************/
size_t FSE_countFast(unsigned* count, unsigned* maxSymbolValuePtr, const unsigned char* src, size_t srcSize);
/* same as FSE_count(), but blindly trust that all values within src are <= maxSymbolValuePtr[0] */

size_t FSE_buildCTable_raw (CTable ct, unsigned nbBits);
/* build a fake CTable, designed to not compress an input, where each symbol uses nbBits */

size_t FSE_buildCTable_rle (CTable ct, unsigned char symbolValue);
/* build a fake CTable, designed to compress always the same symbolValue */

size_t FSE_buildDTable_raw (DTable dt, unsigned nbBits);
/* build a fake DTable, designed to read an uncompressed bitstream where each symbol uses nbBits */

size_t FSE_buildDTable_rle (DTable dt, unsigned char symbolValue);
/* build a fake DTable, designed to always generate the same symbolValue */


/******************************************
*  FSE streaming API
******************************************/
bitD_t FSE_readBitsFast(FSE_DStream_t* bitD, unsigned nbBits);
/* faster, but works only if nbBits >= 1 (otherwise, result will be corrupted) */

unsigned char FSE_decodeSymbolFast(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD);
/* faster, but works only if nbBits >= 1 (otherwise, result will be corrupted) */


#if defined (__cplusplus)
}
#endif
