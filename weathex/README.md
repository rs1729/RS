

#### Weathex WxR-301D

(i) Weathex WxR-301D as seen in Malaysia 2023: 4800 baud w/o PN9.<br />
(ii) There was also a Weathex WxR-301D at [UAII2022](https://github.com/rs1729/RS/tree/test/uaii2022) with slightly different signal/data: 5000 baud PN9.<br />
```
gcc weathex301d.c -o wxr301d
./wxr301d [options] <audio.wav>

options:
    -b       alternative demod
    --json   JSON output
    --pn9

e.g.
(i)  ./wxr301d weathex_fm.wav
(ii) ./wxr301d --pn9 weathex_pn9_fm.wav
```

