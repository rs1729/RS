
## Radiosonde M10

Tools for decoding M10 radiosonde signals.

### Files

* `m10ptu.c` - M10 decoder (trimble GPS) <br />
  `m10gtop.c` - (new) M10 (Gtop GPS)

  ##### Compile
  `gcc m10ptu.c -lm -o m10ptu` <br />
  `gcc m10gtop.c -lm -o m10gtop`

  ##### Usage
  `./m10ptu [options] <audio.wav>` <br />
  * `<audio.wav>`: FM-demodulated signal, recorded as wav audio file <br />
  * `options`: <br />
     `-r`: output raw data <br />
     `-v`, `-vv`: additional data/info (velocities, SN, checksum) <br />
     `-c`: colored output <br />
     `-b`, `-b2`: integrate rawbit-/bit-samples <br />


  ##### Examples
  * `./m10ptu -v 20150701_402MHz.wav` <br />
    `./m10ptu -vv -c 20150701_402MHz.wav` <br />
    `./m10ptu -r -v -c 20150701_402MHz.wav` <br />
    `sox 20150701_402MHz.wav -t wav - lowpass 6000 2>/dev/null | ./m10ptu -vv -c` <br />
    `sox 20150701_402MHz.wav -t wav - highpass 20  2>/dev/null | ./m10ptu -vv -c` <br />
    `./m10ptu -b2 -vv -c 20150701_402MHz.wav` <br />

  If the signal quality is low and (default) zero-crossing-demod is used,
  a lowpass filter is recommended.
  If sample rate is high and timing/sync is not an issue, try integrating the bit-samples (option `-b2`).


 #####
   <br />


* `pilotsonde/m12.c` - Pilotsonde

  ##### Compile
  `gcc m12.c -lm -o m12`

