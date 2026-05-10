#!/bin/bash
set -e
echo "Building spi-master-test..."
gcc -O2 -Wall -I../../../utility -o spi-master-test main.c ../../../utility/bcm2835.c -lrt
echo "Done. Run with: sudo ./spi-master-test [count]"
