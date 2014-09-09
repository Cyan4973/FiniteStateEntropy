Finite State Entropy coder
===========================

Benchmarks
-------------------------

Benchmarks are run using the "full mode" (-b command).
Timing to count symbols, normalize stats, build tables, and read or write headers are included.
Input data is cut into 32KB blocks, to ensure local entropy adaptation.
It allows some better compression ratio at the cost of some speed, due to multiple headers.

FSE is compared to [zlib's Huffman codec](http://zlib.net/)
which also provides "full mode", including headers.

Benchmarks are run on an Intel Core i5-3340M (2.66GHz) platform, with Window Seven 64-bits.
Source code is compiled using MSVC 2012, 64-bits mode.
ProbaXX files are created using the ProbaGen test program.

<table>
  <tr>
    <th>Filename</th><th>Compressor</th><th>Ratio</th><th>Compression</th><th>Decompression</th>
  </tr>
  <tr>
    <th>enwik8.7z</th><th>FSE</th><th>1.000</th><th>1600 MS/s</th><th>4500 MS/s</th>
  </tr>
  <tr>
    <th>enwik8.7z</th><th>zlib</th><th>0.999</th><th>275 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>FSE</th><th>2.690</th><th>300 MS/s</th><th>440 MS/s</th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>zlib</th><th>2.669</th><th>205 MS/s</th><th>220 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba20.bin</th><th>FSE</th><th>2.207</th><th>310 MS/s</th><th>460 MS/s</th>
  </tr>
  <tr>
    <th>proba20.bin</th><th>zlib</th><th>2.190</th><th>245 MS/s</th><th>225 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba50.bin</th><th>FSE</th><th>3.987</th><th>310 MS/s</th><th>450 MS/s</th>
  </tr>
  <tr>
    <th>proba50.bin</th><th>zlib</th><th>3.986</th><th>255 MS/s</th><th>255 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>FSE</th><th>6.316</th><th>310 MS/s</th><th>440 MS/s</th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>zlib</th><th>5.575</th><th>255 MS/s</th><th>275 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>FSE</th><th>15.20</th><th>310 MS/s</th><th>440 MS/s</th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>zlib</th><th>7.175</th><th>250 MS/s</th><th>290 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba95.bin</th><th>FSE</th><th>26.06</th><th>310 MS/s</th><th>440 MS/s</th>
  </tr>
  <tr>
    <th>proba95.bin</th><th>zlib</th><th>7.574</th><th>250 MS/s</th><th>290 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>loong</th><th>FSE</th><th>2045</th><th>1350 MS/s</th><th>4850 MS/s</th>
  </tr>
  <tr>
    <th>loong</th><th>zlib</th><th>7.974</th><th>265 MS/s</th><th>290 MS/s</th>
  </tr>
</table>

*Speed is provided in MS/s (Millions of Symbols per second)*

Speed of FSE is relatively stable accross all tested file, except extreme cases.
*loong* is a special file, which contains large series of identical characters.
It is designed to test the limits of arithmetic coders.
*enwik8.7z* is a non-compressible file, obtained by compressing enwik8 using 7z at maximum setting.

By design, Huffman can't break the "1 bit per symbol" limit, and therefore its compression ratio plateau near 8.
In contrast, FSE remains close to Shannon limit, increasing its compression efficiency as probabilities increases.


