
## Radiosonde decoders

simultaneous decoding


#### Compile
  `gcc -Ofast -c demod_base.c` <br />
  `gcc -O2 -c bch_ecc_mod.c` <br />
  `gcc -O2 -c rs41base.c` <br />
  `gcc -O2 -c dfm09base.c` <br />
  `gcc -O2 -c m10base.c` <br />
  `gcc -O2 -c lms6Xbase.c` <br />
  `gcc -O2 rs_multi.c demod_base.o bch_ecc_mod.o rs41base.o dfm09base.o m10base.o lms6Xbase.o -lm -pthread -o rs_multi`

#### Usage/Examples
  `./rs_multi --rs41 <fq0> --dfm <fq1> --m10 <fq2> --lms <fq3> <iq_baseband.wav>` <br />
  `./rs_multi --rs41 <fq0> --dfm <fq1> --m10 <fq2> --lms <fq3> - <sr> <bs> <iq_baseband.raw>` <br />
  where <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `-0.5 < fq < 0.5`: (relative) frequency, `fq=freq/sr` <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<sr>`: sample rate <br />
  &nbsp;&nbsp;&nbsp;&nbsp; `<bs>=8,16,32`: bits per (real) sample (u8, s16 or f32) <br />
  decodes up to `MAX_FQ=5 (demod_base.h)` signals. Decoding more signals than number of CPUs/cores is not recommended. <br />
  c.f.
  https://youtu.be/5YXP9LYUgLs

