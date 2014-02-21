Finite State Entropy coder
===========================

FSE is a new kind of [Entropy encoder](http://en.wikipedia.org/wiki/Entropy_encoding),
based on [ANS theory, from Jarek Duda](http://arxiv.org/abs/1311.2540).

It is designed to compete with [Huffman encoder](http://en.wikipedia.org/wiki/Huffman_coding)
and [Arithmetic ones](http://en.wikipedia.org/wiki/Arithmetic_coding).

While huffman is fast but can only represent probabilities in power of 2 (50%, 25%, etc.)
arithmetic coding can represent probabilities with much better accuracy, but requires multiplications and divisions.

FSE solve this dilemna by providing precise probabilities, like arithmetic does,
but using only *additions, masks and shifts*.

This makes FSE faster, on par with Huffman speed, and even suitable for low-power CPU environment.


Benchmarks
-------------------------

Benchmarks are run on an Intel Core i5-3340M (oc'ed to 2.9GHz), with Window Seven 64-bits.
Source code is compiled using MSVC 2012, 64-bits mode.
Core loop results are reported ( FSE_compress_usingCTable() & FSE_decompress_usingDTable() )

<table>
  <tr>
    <th>Filename</th><th>Compressor</th><th>Ratio</th><th>Compression</th><th>Decompression</th>
  </tr>
  <tr>
    <th>book1</th><th>FSE</th><th>1.764</th><th>420 MS/s</th><th>520 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>FSE</th><th>2.688</th><th>420 MS/s</th><th>520 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>FSE</th><th>6.313</th><th>420 MS/s</th><th>520 MS/s</th>
  </tr>
  <tr>
    <th></th><th></th><th></th><th></th><th></th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>FSE</th><th>15.21</th><th>420 MS/s</th><th>520 MS/s</th>
  </tr>
</table>

*Speed is provided in MS/s (Millions of Symbols per second).
For more detailed results, browse to [data directory](data)*

As an obvious outcome, speed of FSE is stable accross all tested file.
By design, Huffman can't break the "1 bit per symbol" limit.
FSE is free of such limit, so its performance increase with probability, remaining close to Shannon limit.

