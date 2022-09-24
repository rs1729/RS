
## Radiosonde decoders

simultaneous decoding


#### Compile
  `gcc -Ofast -c demod_base.c` <br />
  `gcc -O2 -c bch_ecc_mod.c` <br />
  `gcc -O2 -c rs41base.c` <br />
  `gcc -O2 -c dfm09base.c` <br />
  `gcc -O2 -c m10base.c` <br />
  `gcc -O2 -c m20base.c` <br />
  `gcc -O2 -c lms6Xbase.c` <br />
  `gcc -O2 rs_multi.c demod_base.o bch_ecc_mod.o rs41base.o dfm09base.o m10base.o m20base.o lms6Xbase.o \`<br />
  &nbsp;&nbsp;&nbsp;&nbsp; `-lm -pthread -o rs_multi`

#### Usage/Examples
  `$ ./rs_multi --rs41 <fq0> --dfm <fq1> --m10 <fq2> --lms <fq3> <iq_baseband.wav>` <br />
  `$ ./rs_multi --rs41 <fq0> --dfm <fq1> --m10 <fq2> --lms <fq3> - <sr> <bs> <iq_baseband.raw>` <br />
  where <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `-0.5 < fqX < 0.5`: (relative) frequency, `fq=freq/sr` <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<sr>`: sample rate <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<bs>=8,16,32`: bits per (real) sample (u8, s16 or f32) <br />
  decodes up to `MAX_FQ=5 (demod_base.h)` signals. Decoding more signals than number of CPUs/cores is not recommended.<br />
  Note: If the baseband sample rate has no appropriate factors (e.g. if prime), the IF sample rate might be high and IF-processing slow.<br />

  Sending add/remove commands via fifo: <br />
  [terminal 1]<br />
  `$ rtl_sdr -f 403.0M -s 1920000 - | ./rs_multi --fifo rsfifo --rs41 <fq0> --dfm <fq1> - 1920000 8`<br />
  where `<fqX>` is the (relative) frequency of signal `X`<br />
  [terminal 2]<br />
  *add M10 on `<fq2>`*:<br /> `$ echo "m10 <fq2>" > rsfifo`<br />
  *remove `<fq1>`*:<br /> `$ echo "-1" > rsfifo`<br />

  If there is no decode for `SEC_NO_SIGNAL=10 (demod_base.c)` seconds, the signal is removed,
  unless option `-c` is used.<br />
  `--json` output is also possible.

