
## Radiosonde DFM-06/DFM-09

Based on information already available at<br />
https://www.amateurfunk.uni-kl.de/projekte-aktivitaeten/decoder-wettersonden/

#### Files

  * `dfm06ptu.c`

#### Compile
  `gcc dfm06ptu.c -lm -o dfm06ptu`

#### Usage
  `./dfm06ptu [options] <audio.wav>` <br />
  * `<audio.wav>`: FM-demodulated signal, recorded as wav audio file
  * `options`: <br />
      `-i`: invert signal/polarity (DFM-09) <br />
      `-b`, `-b2`: integrate rawbit-/bit-samples <br />
      `-r`: output raw data <br />
     `-v`: additional info <br />
     `--ecc`: Hamming code error correction <br />
     `--ptu`: temperature <br />


#### Examples
  * `./dfm06ptu --ecc --ptu -v dfm-audio.wav`

  FSK-demodulation is kept very simple. If the signal quality is low and (default) zero-crossing-demod is used,
  a lowpass filter is recommended:
  * `sox dfm-audio.wav -t wav - lowpass 2000 2>/dev/null | ./dfm06ptu --ecc --ptu -v`

  If timing/sync is not an issue, integrating the bit-samples (option `-b2`) is better for error correction:
  * `./dfm06ptu -b2 --ecc --ptu -v dfm-audio.wav`

  For DFM-09 or if the signal is inverted
  (depends on sdr-software and/or audio-card/settings), try option `-i`.


####  Error correction
  Use error correction `--ecc` together with option `-b2`.
  The codewords are 8-bit long and the \[8,4\] extended Hamming code can correct one-bit errors
  and detect two-bit errors. If there are more than two errors, it is possible that the codeword is
  corrected to a different codeword closer to the received word. The minimum distance is 4.
  Thus if the signal level is low, the decoding can go wrong.
  A high number of codewords with bit-errors in a frame could indicate unreliable decoding.

