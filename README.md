Finite State Entropy coder
===========================

FSE is a new kind of [Entropy encoder](http://en.wikipedia.org/wiki/Entropy_encoding),
based on [ANS theory, from Jarek Duda](http://arxiv.org/abs/1311.2540).

It is designed to compete with [Huffman encoder](http://en.wikipedia.org/wiki/Huffman_coding)
and [Arithmetic ones](http://en.wikipedia.org/wiki/Arithmetic_coding).

While huffman is fast but can only represent probabilities in power of 2 (50%, 25%, 12.5%, etc.)
arithmetic coding can represent probabilities with much better accuracy, but requires multiplications and divisions.

FSE solve this dilemna by providing precise probabilities, like arithmetic does,
but using only *additions, masks and shifts*.

This makes FSE suitable for low-power CPU environment, including "retro" ones (from the 80's).
Even modern high-power CPU will benefit from FSE thanks to its better speed.


Benchmarks
-------------------------

Benchmarks are run on an Intel Core i5-3340M (2.7GHz) platform, with Window Seven 64-bits.
Source code is compiled using MSVC 2012, 64-bits mode.
It compares FSE to [Huff0](http://fastcompression.blogspot.fr/p/huff0-range0-entropy-coders.html), a fast Huffman implementation.

<table>
  <tr>
    <th>Filename</th><th>Compressor</th><th>Ratio</th><th>Compression</th><th>Decompression</th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>FSE</th><th>2.684</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>Huff0</th><th>2.673</th><th>165 MS/s</th><th>165 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>FSE</th><th>6.313</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>Huff0</th><th>5.574</th><th>190 MS/s</th><th>190 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>FSE</th><th>15.19</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>Huff0</th><th>7.176</th><th>190 MS/s</th><th>190 MS/s</th>
  </tr>
</table>

*Speed is provided in MS/s (Millions of Symbols per second).
For more detailed results, browse to [data directory](data)*

By design, Huffman can't break the "1 bit per symbol" limit, and therefore loses efficiency when probabilities improve.
In contrast, FSE keeps increasing its performance, remaining close to Shannon limit.

