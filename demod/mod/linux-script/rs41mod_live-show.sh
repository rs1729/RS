#skontrolovat prehravacie zariadenie cez pulseaudio
sox -t alsa default -t wav - 2>/dev/null | ./rs41mod --ecc4 --ptu2 --dewp -vx
