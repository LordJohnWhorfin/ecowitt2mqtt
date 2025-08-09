CC=gcc
CFLAGS=-Wall -O2
LIBS=/usr/lib/x86_64-linux-gnu/libmosquitto.so

all: ecowitt2mqtt

ecowitt2mqtt: ecowitt2mqtt.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f ecowitt2mqtt

install:
	mv ecowitt2mqtt /usr/local/bin/ecowitt2mqtt
