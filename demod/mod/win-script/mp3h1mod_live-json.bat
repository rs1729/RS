sox.exe -q -t waveaudio "CABLE Output" -t wav - 2>nul | mp3h1mod.exe --ecc --ptu --json >> mp3h1_data_%date:~-4,4%%date:~-7,2%%date:~-10,2%-%time:~-11,2%%time:~-8,2%%time:~-5,2%.json
