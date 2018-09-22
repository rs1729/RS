
## Radiosonde LMS6

LMS6 (403 MHz) <br />
(for 1680 MHz 9600 bit/s try /RS/mk2a)

FSK 4800 bit/s <br />
R=1/2 K=7 convolutional code, Reed-Solomon RS(255,223)-blocks (CCSDS)

#### Files

  * `lms6ccsds.c`, `RS/ecc/bch_ecc.c`

#### Compile
  (copy `bch_ecc.c`) <br />
  `gcc lms6ccsds.c -lm -o lms6ccsds`

#### Usage
  `./lms6ccsds -b -v --vit --ecc <audio.wav>` <br />
  * `<audio.wav>`: FM-demodulated signal, recorded as wav audio file
  * `options`: <br />
      `-b`: integrate bit-samples <br />
      `-r`: output raw data <br />
      `-v`: additional data (sonde-ID) <br />
     `--vit`: Viterbi decode <br />
     `--ecc`: Reed-Solomon error correction <br />

Integrating bit-samples is better for error correction. Good synchronization is important. Correlation locates
block/frame start even better (cf. /RS/demod).

#### older versions

  * `lms6.c`, `lms6ecc.c`

