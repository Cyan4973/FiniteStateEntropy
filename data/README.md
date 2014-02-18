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

Benchmarks are run on an Intel Core i5-3340M (2.7GHz) platform, with Window Seven 64-bits.
Source code is compiled using MSVC 2012, 64-bits mode.
ProbaXX files are created using the ProbaGen test program.

<table>
  <tr>
    <th>Filename</th><th>Compressor</th><th>Ratio</th><th>Compression</th><th>Decompression</th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>FSE</th><th>2.693</th><th>265 MS/s</th><th>415 MS/s</th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>zlib</th><th>2.669</th><th>205 MS/s</th><th>220 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba10.bin</th><th>FSE</th><th>1.696</th><th>275 MS/s</th><th>430 MS/s</th>
  </tr>
  <tr>
    <th>proba10.bin</th><th>zlib</th><th>1.688</th><th>230 MS/s</th><th>220 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba20.bin</th><th>FSE</th><th>2.207</th><th>275 MS/s</th><th>430 MS/s</th>
  </tr>
  <tr>
    <th>proba20.bin</th><th>zlib</th><th>2.190</th><th>245 MS/s</th><th>225 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba30.bin</th><th>FSE</th><th>2.712</th><th>275 MS/s</th><th>425 MS/s</th>
  </tr>
  <tr>
    <th>proba30.bin</th><th>zlib</th><th>2.692</th><th>250 MS/s</th><th>235 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba40.bin</th><th>FSE</th><th>3.283</th><th>275 MS/s</th><th>425 MS/s</th>
  </tr>
  <tr>
    <th>proba40.bin</th><th>zlib</th><th>3.187</th><th>250 MS/s</th><th>235 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba50.bin</th><th>FSE</th><th>3.987</th><th>275 MS/s</th><th>425 MS/s</th>
  </tr>
  <tr>
    <th>proba50.bin</th><th>zlib</th><th>3.986</th><th>255 MS/s</th><th>255 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba60.bin</th><th>FSE</th><th>4.916</th><th>275 MS/s</th><th>425 MS/s</th>
  </tr>
  <tr>
    <th>proba60.bin</th><th>zlib</th><th>4.777</th><th>255 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>FSE</th><th>6.313</th><th>275 MS/s</th><th>420 MS/s</th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>zlib</th><th>5.575</th><th>255 MS/s</th><th>275 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba80.bin</th><th>FSE</th><th>8.802</th><th>275 MS/s</th><th>420 MS/s</th>
  </tr>
  <tr>
    <th>proba80.bin</th><th>zlib</th><th>6.375</th><th>250 MS/s</th><th>280 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>FSE</th><th>15.21</th><th>275 MS/s</th><th>420 MS/s</th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>zlib</th><th>7.175</th><th>250 MS/s</th><th>290 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba95.bin</th><th>FSE</th><th>26.08</th><th>270 MS/s</th><th>420 MS/s</th>
  </tr>
  <tr>
    <th>proba95.bin</th><th>zlib</th><th>7.574</th><th>250 MS/s</th><th>290 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>loong</th><th>FSE</th><th>2045</th><th>1300 MS/s</th><th>4600 MS/s</th>
  </tr>
  <tr>
    <th>loong</th><th>zlib</th><th>7.974</th><th>265 MS/s</th><th>290 MS/s</th>
  </tr>
</table>

*Speed is provided in MS/s (Millions of Symbols per second)*

As an obvious outcome, speed of FSE is stable accross all tested file.
By design, Huffman can't break the "1 bit per symbol" limit, and therefore loses efficiency when probabilities improve.
In contrast, FSE keeps increasing its performance, remaining close to Shannon limit.

*loong* is a special file, which contains large series of identical characters.
It is designed to test the limits of arithmetic coders.

