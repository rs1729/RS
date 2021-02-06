./rs41mod --ecc4 -vv --ptu2 --dewp "$@" > rs41_data_all.txt 
./rs41mod --ecc4 --ptu2 --dewp "$@" > rs41_data.txt 
./rs41mod --ecc4 --sat "$@" > rs41_data-sat.txt 
./rs41mod --ecc4 --ptu2 --dewp --json "$@" > rs41_data.json 
