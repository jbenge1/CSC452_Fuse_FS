#!/bin/bash

make clean
fusermount3 -u testmount 2>&1 > /dev/null
dd bs=1K count=5K if=/dev/zero of=.disk
make
./csc452 -d testmount/
