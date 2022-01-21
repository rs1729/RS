cp "$1" "$1""a"
cat "$1" >> "$1""a"
./rs41mod --ecc4 -vv --ptu2 --dewp --aux --rawhex "$1" > "$1"_data_all.txt 
./rs41mod --ecc4 --ptu2 --dewp --aux --rawhex "$1" > "$1"_data.txt 
./rs41mod --ecc4 --sat --rawhex "$1" > "$1"_data-sat.txt 
./rs41mod --ecc4 --ptu2 --dewp --json2 --aux --silent --rawhex "$1" > "$1"_data.json
./rs41mod --ecc4 --ptu2 --dewp --json2 --aux --silent --rawhex "$1""a" > "$1"_data1.json 
rm "$1""a"
