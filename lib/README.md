Entropy codec library
===========================

The __lib__ directory contains several files, but you don't necessarily want them all.
Here is a detailed list, to help you decide which one you need :

#### Compulsory files

These files are required in all circumstances :
__error.h__ : error list and management
__mem.h__ : low level memory access routines
__bitstream.h__ : generic read/write bitstream common to all entropy codecs

#### Finite State Entropy

This is the base codec required by other ones.
It implements a tANS variant, similar to arithmetic in compression performance, but much faster.
__fse.c__ implements the codec, while __fse.h__ exposes its interfaces.
__fse_static.h__ is an optional header, exposing unsupported and potentially unstable interfaces, for experiments.

#### FSE 16-bits version

This codec is able to encode alphabets of size > 256, using 2 bytes per symbol.
__fseU16.c__ implements the codec, while __fseU16.h__ exposes its interfaces.
This codec also requires the basic FSE codec to compile properly.

#### Huff0 Huffman codec

This is the fast huffman codec.
__huff0.c__ implements the codec, while __huff0.h__ exposes its interfaces.
__huff0_static.h__ is an optional header, exposing unsupported and potentially unstable interfaces, for experiments.
This codec also requires the basic FSE codec to compress its headers.

