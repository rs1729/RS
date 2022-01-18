copy %1 %1"a"
type %1 >> %1"a"
rs41mod.exe -vv --ptu2 --dewp --rawhex --aux --ecc4 %1 > %1_data_all.txt 
rs41mod.exe --ptu2 --dewp --rawhex --aux --ecc4 %1 > %1_data.txt 
rs41mod.exe --sat --rawhex --ecc4 %1 > %1_data-sat.txt 
rs41mod.exe --ptu2 --dewp --json --aux --silent --rawhex --ecc4 %1 > %1_data.json 
rs41mod.exe --ptu2 --dewp --json --aux --silent --rawhex --ecc4 %1"a" > %1_data1.json
del %1"a"
