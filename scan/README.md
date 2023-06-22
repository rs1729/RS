
## detect radiosonde type

#### Files

  * `dft_detect.c`

#### Compile
  `gcc dft_detect.c -lm -o dft_detect`

#### Usage
  `./dft_detect [options] <file>` <br />
  options:<br />
  &nbsp;&nbsp;&nbsp;&nbsp; `--IQ <fq>` &nbsp;:&nbsp; IQ data input, where `<fq>` is the relative frequency in `-0.5..0.5` <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `-v` &nbsp;:&nbsp; sample count <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `-c` &nbsp;:&nbsp; continuous output<br />
  &nbsp;&nbsp;&nbsp;&nbsp; `-t <sec>` &nbsp;:&nbsp; time limit <br />

  (input `<file>` can be `stdin`)
  <br />

  FM data:<br />
  `./dft_detect <fm_audio.wav>` <br />

  IQ data:<br />
  `./dft_detect --IQ <fq> <iq_data.wav>` <br />
  For IQ data (i.e. 2 channels) it is possible to read raw data (without wav header): <br />
  `./dft_detect --IQ <fq> - <sr> <bs> <iq_data.raw>` <br />
  where <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<sr>` &nbsp;:&nbsp; sample rate <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<bs>=8,16,32` &nbsp;:&nbsp; bits per (real) sample (u8, s16 or f32)

  `dft_detect` stops, if a (potential) radiosonde signal is detected, or if a time limit is set with option `-t`. <br />
  With option `-c` detection is continued, but a time limit can be set with `-t`.

  Output is of the form `<TYPE>: <score>` where the correlation score is a normalized value in `-1..+1`.

#### Examples

Ex.1
```
$ ./dft_detect fm_audio.wav
sample_rate: 48000
bits       : 8
channels   : 1
RS41: 0.9885
```
Ex.2
```
$ ./dft_detect -c fm_audio.wav 2>/dev/null
RS41: 0.9885
RS41: 0.9851
RS41: 0.9784
RS41: 0.9869
RS41: 0.9845
RS41: 0.9828
RS41: 0.9814
RS41: 0.9821
RS41: 0.9823
RS41: 0.9882
[...]
```

Ex.3
```
$ ./dft_detect -c -t 4 fm_audio.wav 2>/dev/null
RS41: 0.9885
RS41: 0.9851
RS41: 0.9784
RS41: 0.9869
RS41: 0.9845
```

Ex.4
```
$ ./dft_detect -v -c -t 2 fm_audio.wav 2>/dev/null
sample: 39801
RS41: 0.9885
sample: 87824
RS41: 0.9851
sample: 135831
RS41: 0.9784
```

Ex.5<br />
Some radiosonde types have similar signals, false detection is possible.
```
$ ./dft_detect -c -t 4 fm_meisei.wav 2>/dev/null
MEISEI: 0.9802
MRZ: -0.9720
MEISEI: 0.9829
```
Here a Meisei radiosonde seems to be more likely.

Ex.6<br />
Confirmed detection (two hits):
```
$ ./dft_detect -d2 -t 4 fm_meisei.wav 2>/dev/null
MEISEI: 0.9829
```

