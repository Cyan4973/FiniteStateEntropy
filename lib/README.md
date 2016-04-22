New Generation Entropy library
==============================

The __lib__ directory contains several files, but you don't necessarily want them all.
Here is a detailed list, to help you decide which one you need :

#### Compulsory files

These files are required in all circumstances :
- __error.h__ : error list and management
- __mem.h__ : low level memory access routines
- __bitstream.h__ : generic read/write bitstream common to all entropy codecs

#### Finite State Entropy

This is the base codec required by other ones.
It implements a tANS variant, similar to arithmetic in compression performance, but much faster.
- __fse.c__ implements the codec, while __fse.h__ exposes its interfaces.
- __fse_static.h__ is an optional header, exposing unsupported and potentially unstable interfaces, for experiments.

#### FSE 16-bits version

This codec is able to encode alphabets of size > 256, using 2 bytes per symbol. It requires the base FSE codec to compile properly.
- __fseU16.c__ implements the codec, while __fseU16.h__ exposes its interfaces.


#### Huffman codec

This is the fast huffman codec. This requires the base FSE codec to compress its headers.
- __huf.c__ implements the codec, while __huf.h__ exposes its interfaces.
- __huf_static.h__ is an optional header, exposing unsupported and potentially unstable interfaces, for experiments.


