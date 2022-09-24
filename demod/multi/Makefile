
CC = gcc
COPTS = -O3

rs_multi: rs41base.o lms6Xbase.o dfm09base.o m10base.o m20base.o demod_base.o bch_ecc_mod.o
	$(CC) $(COPTS) -o rs_multi rs_multi.c demod_base.o bch_ecc_mod.o rs41base.o dfm09base.o m10base.o m20base.o lms6Xbase.o -lm -pthread

rs41base.o: rs41base.c
	$(CC) $(COPTS) -c rs41base.c

dfm09base.o: dfm09base.c
	$(CC) $(COPTS) -c dfm09base.c

m10base.o: m10base.c
	$(CC) $(COPTS) -c m10base.c

m20base.o: m20base.c
	$(CC) $(COPTS) -c m20base.c

lms6Xbase.o: lms6Xbase.c
	$(CC) $(COPTS) -c lms6Xbase.c

demod_base.o: demod_base.c demod_base.h
	$(CC) -Ofast -c demod_base.c

bch_ecc_mod.o: bch_ecc_mod.c bch_ecc_mod.h
	$(CC) $(COPTS) -c bch_ecc_mod.c

.PHONY: clean
clean:
	rm -f rs_multi
	rm -f rs41base.o dfm09base.o m10base.o m20base.o lms6Xbase.o
	rm -f demod_base.o bch_ecc_mod.o

