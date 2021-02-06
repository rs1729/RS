./rs41mod --ecc4 -vv --ptu2 --dewp --rawhex "$@" > rs41_data_all.txt 
./rs41mod --ecc4 --ptu2 --dewp --rawhex "$@" > rs41_data.txt 
./rs41mod --ecc4 --sat --rawhex "$@" > rs41_data-sat.txt 
./rs41mod --ecc4 --ptu2 --dewp --json --rawhex "$@" > rs41_data.json 
