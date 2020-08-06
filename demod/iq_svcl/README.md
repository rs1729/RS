
## IQ server/client

receive IF stream from baseband IQ via TCP,
`PORT=1280 (iq_svcl.h)`


#### Compile
  `gcc -Ofast -c iq_base.c` <br />
  `gcc -O2 iq_server.c iq_base.o -lm -pthread -o iq_server`<br />
  `gcc -O2 iq_client.c -o iq_client` <br />

#### Usage/Examples
  - Ex.1<br />
  [terminal 1]<br />
  `$ ./iq_server --bo 32 <iq_baseband.wav>` &nbsp;&nbsp;
  (or &nbsp; `$ ./iq_server --bo 32 - <sr> <bs> <iq_baseband.raw>`)<br />
  [terminal 2]<br />
  `$ ./iq_client --freq <fq> | ./rs41mod -vx --IQ 0.0 --lp - <if_sr> <bo>` <br />
  where <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `-0.5 < fq < 0.5`: (relative) frequency, `fq=frequency/sr` <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<if_sr>`: IF sample rate <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<bo>=8,16,32`: output/IF bits per (real) sample (u8, s16 or f32) <br />
  down-converts up to `MAX_FQ=(4+1) (iq_base.h)` channels/signals. More signals than number of CPUs/cores is not recommended.
  One channel can be used for scanning, `--scan` makes FFT (2 seconds).<br />
  If no output bps is chosen (`--bo [8,16,32]`), the IF bps is equal to the baseband bps. It is recommended to use
  `--bo 32` (i.e. float32) output, then no quantization noise is introduced when converting from internal float32 samples.<br />

  - Ex.2<br />
  [terminal 1]<br />
  `$ rtl_sdr -f 403.0M -s 1920000 - | ./iq_server --scan --bo 32 - 1920000 8`<br />
  [terminal 2]<br />
  `$ ./iq_client --freq -0.3125 | ./m10mod -c -vv --IQ 0.0 - 48000 32`<br />
  [terminal 3]<br />
  `$ ./iq_client --freq 0.0 | ./rs41mod -vx --IQ 0.0 - 48000 32`<br />
  [terminal 4]<br />
  `$ ./iq_client -1` &nbsp;&nbsp; (*close channel 1*)<br />
  `$ ./iq_client --stop`<br />
  The `--scan` option immediately starts reading the IQ stream and buffering is reduced.
  `./iq_client --scan` can also request FFT.<br />
  The FFT is saved in `db_scan.txt` as `fq;frequency;dB`.

  The IF sample rate `if_sr` is at least 48000 such that the baseband sample rate `sr` is a multiple of `if_sr`.

