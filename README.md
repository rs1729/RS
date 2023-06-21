RS
==
  
#### Wetterballon-Radiosonden  400-406 MHz  

* Decoder: <br />
  (compile: `gcc <decoder.c> -lm -o <decoder>`)

  `RS/rs92`: RS92-SGP, RS92-AGP <br />
  `RS/rs41`: RS41-SG(P) <br />
  `RS/dropsonde`: RD94 <br />
  `RS/m10`: M10, M20 <br />
  `RS/dfm`: DFM-06, DFM-09, DFM-17 <br />
  `RS/imet`: iMet-1-AB, iMet-1-RS (iMet-4), iMet-54 <br />
  `RS/c34`: C34, C50 <br />
  `RS/lms6`: LMS6 (403 MHz) <br />
  `RS/mk2a`: MkIIa (LMS6-1680MHz) <br />
  `RS/meisei`: Meisei (iMS-100, RS-11G) <br />

  `RS/demod/mod`: alternative decoders using cross-correlation for header-synchronization, FM/IQ data;<br />
  &nbsp;&nbsp;&nbsp;&nbsp; RS41, M10/M20, DFM-06/09/17, LMS6-403, Meisei (iMS-100, RS-11G), iMet-54, RS92-SGP, MRZ-N1, MTS01


  `RS/tree/test/uaii2022`: [UAII2022](https://github.com/rs1729/RS/tree/test/uaii2022): CF-06AH, HT03G, WxR-301D, PS-B3, ATMS-3710 <br />

  `RS/tree/test/weathex`: Weathex w/o PN9 <br />


  `RS/ecc`: error correction codes (Reed-Solomon/BCH) <br />


  Die Decoder erwarten das FM-demodulierte wav-Audio des empfangenen Signals (kann auch mit 
sox gestreamt werden). Die weitere Demodulation ist sehr einfach gehalten (Nulldurchgaenge), 
so dass die Decodierung empfindlich auf Stoerungen reagiert und ein gutes Signal braucht. 
Oft hilft schon, z.B. mit sox einen lowpass-Filter zwischenzuschalten (fuer C34/C50 und iMet-1-RS
wird DFT verwendet). Je nach Empfangsgeraet oder SDR-Software kann das Signal invertiert sein 
(ebenso fuer neuere DFM-09 gegenueber DFM-06).


* Diverses:

  `RS/IQ`: Beispiele für Behandlung von IQ-Signalen <br />
  `RS/scan`: einfaches Beispiel, wie man mit rtl_sdr-tools automatisch scannen kann <br />


* Videos & Bilder:

  https://www.youtube.com/user/boulderplex  
  https://www.flickr.com/photos/116298535@N06/albums/72157644377585863  


* erläuternde Beiträge:

  https://www.fingers-welt.de/phpBB/viewtopic.php?p=50955#p50955  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=64707#p64707  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=75202#p75202  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=87987#p87987  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=88325#p88325  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=88845#p88845  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=155677#p155677  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=163997#p163997  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=193107#p193107  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=196322#p196322  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=198064#p198064  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=198380#p198380  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=203315#p203315  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=235868#p235868  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=245177#p245177  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=272805#p272805  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=336249#p336249  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=354503#p354503  
  https://github.com/rs1729/RS/issues/32#issuecomment-792334791  
  https://www.fingers-welt.de/phpBB/viewtopic.php?p=409627#p409627  


