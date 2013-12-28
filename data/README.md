Finite State Entropy coder
===========================

Benchmarks
-------------------------

Benchmarks are run on an Intel Core i5-3340M (2.7GHz) platform, with Window Seven 64-bits.
Source code is compiled using MSVC 2012, 64-bits mode.
ProbaXX files are created using the ProbaGen test program.

<table>
  <tr>
    <th>Filename</th><th>Compressor</th><th>Ratio</th><th>Compression</th><th>Decompression</th>
  </tr>
  <tr>
    <th>win98-lz4-run</th><th>FSE</th><th>2.684</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba10.bin</th><th>FSE</th><th>1.695</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba20.bin</th><th>FSE</th><th>2.206</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba30.bin</th><th>FSE</th><th>2.711</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba40.bin</th><th>FSE</th><th>3.283</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba50.bin</th><th>FSE</th><th>3.987</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba60.bin</th><th>FSE</th><th>4.916</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba70.bin</th><th>FSE</th><th>6.313</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba80.bin</th><th>FSE</th><th>8.794</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba90.bin</th><th>FSE</th><th>15.19</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>proba95.bin</th><th>FSE</th><th>26.04</th><th>195 MS/s</th><th>265 MS/s</th>
  </tr>
  <tr>
    <th>loong</th><th>FSE</th><th>1656</th><th>1.20 GS/s</th><th>3.43 GS/s</th>
  </tr>
</table>

*Speed is provided in MS/s (Millions of Symbols per second)
or GS/s (Giga=Billions of Symbols per second)*

As an obvious outcome, speed of FSE is stable accross all tested file.
*loong* is a special file, which contains large series of identical characters.
It is designed to test the limits of arithmetic coders.

