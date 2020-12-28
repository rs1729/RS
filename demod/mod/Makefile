
CC = gcc
COPTS = -O3

.PHONY: all
all: rs41mod rs92mod lms6Xmod meisei100mod dfm09mod m10mod mXXmod

rs41mod: rs41mod.c demod_mod.o bch_ecc_mod.o
	$(CC) $(COPTS) -o rs41mod rs41mod.c demod_mod.o bch_ecc_mod.o -lm

dfm09mod: dfm09mod.c demod_mod.o
	$(CC) $(COPTS) -o dfm09mod dfm09mod.c demod_mod.o -lm

m10mod: m10mod.c demod_mod.o
	$(CC) $(COPTS) -o m10mod m10mod.c demod_mod.o -lm

mXXmod: mXXmod.c demod_mod.o
	$(CC) $(COPTS) -o mXXmod mXXmod.c demod_mod.o -lm

lms6Xmod: lms6Xmod.c demod_mod.o bch_ecc_mod.o
	$(CC) $(COPTS) -o lms6Xmod lms6Xmod.c demod_mod.o bch_ecc_mod.o -lm

meisei100mod: meisei100mod.c demod_mod.o bch_ecc_mod.o
	$(CC) $(COPTS) -o meisei100mod meisei100mod.c demod_mod.o bch_ecc_mod.o -lm

rs92mod: rs92mod.c demod_mod.o bch_ecc_mod.o nav_gps_vel.c
	$(CC) $(COPTS) -o rs92mod rs92mod.c demod_mod.o bch_ecc_mod.o -lm

demod_mod.o: demod_mod.c demod_mod.h
	$(CC) -Ofast -c demod_mod.c

bch_ecc_mod.o: bch_ecc_mod.c bch_ecc_mod.h
	$(CC) $(COPTS) -c bch_ecc_mod.c

.PHONY: clean
clean:
	rm -f rs41mod rs92mod lms6Xmod meisei100mod dfm09mod m10mod mXXmod
	rm -f demod_mod.o
	rm -f bch_ecc_mod.o

