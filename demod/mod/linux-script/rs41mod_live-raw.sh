#skontrolovat prehravacie zariadenie cez pulseaudio
sox -t alsa default -t wav - 2>/dev/null | ./rs41mod --ecc4 -r >> rs41_raw_`date +%Y%m%d-%H%M%S`_raw.txt
