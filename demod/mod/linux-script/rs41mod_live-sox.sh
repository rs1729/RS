sox -t alsa default -t wav - 2>/dev/null | ./rs41mod --ecc4 -r | tee -a rs41_raw_`date +%Y%m%d-%H%M%S`.txt | ./rs41mod --ecc4 --ptu2 --dewp -vx --rawhex | tee -a rs41_data_`date +%Y%m%d-%H%M%S`.txt
