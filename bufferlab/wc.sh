#!/bin/bash

gcc -c -m32 wc.s
objdump -d wc.o | tee bytecode
./wc.py < bytecode
