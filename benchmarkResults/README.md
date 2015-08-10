Finite State Entropy coder
===========================

Benchmarks
-------------------------

Benchmarks are run using the "full mode" (-b command).
Timing to count symbols, normalize stats, build tables, and read or write headers are included.
Input data is cut into 32KB blocks, to ensure local entropy adaptation.
It improves compression ratio on non-static sources at the cost of some speed, due to multiple headers.

FSE is compared to Huff0 and [zlib's Huffman codec](http://zlib.net/).
They all also provide "full mode", including headers.

Benchmarks are run on an Intel Core i7-5600U (2.6GHz) platform, with Linux Mint17 64-bits.
Source code is compiled using GCC 4.8.4, 64-bits mode.

|             | Ratio | Compression | Decompression |
|:-----------:| -----:| -----------:| -------------:|
|[_book1_][1] |   --- |         --- |           --- |
| FSE         | 1.758 |    320 MB/s |      480 MB/s |
| Huff0       | 1.751 |    550 MB/s |      590 MB/s |
| zlibH       | 1.752 |    250 MB/s |      240 MB/s |
|_proba20.bin_|   --- |         --- |           --- |
| FSE         | 2.211 |    320 MB/s |      480 MB/s |
| Huff0       | 2.193 |    570 MB/s |      595 MB/s |
| zlibH       | 2.193 |    260 MB/s |      245 MB/s |
|_proba80.bin_|   --- |         --- |           --- |
| FSE         | 8.838 |    315 MB/s |      450 MB/s |
| Huff0       | 6.384 |    565 MB/s |      620 MB/s |
| zlibH       | 6.380 |    270 MB/s |      310 MB/s |
|_enwik8.7z_  |   --- |         --- |           --- |
| FSE         | 1.000 |   2250 MB/s |     4400 MB/s |
| Huff0       | 1.000 |   2250 MB/s |     4900 MB/s |
| zlibH       | 0.999 |    280 MB/s |      275 MB/s |

[1]:http://corpus.canterbury.ac.nz/descriptions/calgary/book1.html

ProbaXX files are created using the ProbaGen test program.
*enwik8.7z* is a non-compressible file, obtained by compressing enwik8 using 7z at maximum setting.

Speed of FSE is relatively stable accross all tested file (except extreme cases).
By design, Huffman can't break the "1 bit per symbol" limit, and therefore its compression ratio plateau near 8.
In contrast, FSE remains close to Shannon limit, increasing its compression efficiency as probabilities increases.


