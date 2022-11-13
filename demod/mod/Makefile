PROGRAMS := rs41mod rs92mod lms6Xmod meisei100mod dfm09mod m10mod mXXmod imet54mod mp3h1mod mts01mod

LDLIBS = -lm
CC = gcc
CFLAGS = -O3

.PHONY: all
all: $(PROGRAMS)

rs41mod: rs41mod.o demod_mod.o bch_ecc_mod.o

dfm09mod: dfm09mod.o demod_mod.o

m10mod: m10mod.o demod_mod.o

mXXmod: mXXmod.o demod_mod.o

imet54mod: imet54mod.o demod_mod.o

lms6Xmod: lms6Xmod.o demod_mod.o bch_ecc_mod.o

meisei100mod: meisei100mod.o demod_mod.o bch_ecc_mod.o

rs92mod: rs92mod.o demod_mod.o bch_ecc_mod.o

mp3h1mod: mp3h1mod.o demod_mod.o

mts01mod: mts01mod.o demod_mod.o

demod_mod.o: CFLAGS += -Ofast
demod_mod.o: demod_mod.h

bch_ecc_mod.o: bch_ecc_mod.h

.PHONY: clean
clean:
	rm -f $(PROGRAMS) $(PROGRAMS:=.o)
	rm -f demod_mod.o
	rm -f bch_ecc_mod.o

