CFLAGS = -O2 -Wall -I../../../utility
LDFLAGS = -lrt

spi-master-test: main.c ../../../utility/bcm2835.c
	$(CC) $(CFLAGS) -o $@ main.c ../../../utility/bcm2835.c $(LDFLAGS)

clean:
	rm -f spi-master-test

run: spi-master-test
	sudo ./spi-master-test

.PHONY: clean run
