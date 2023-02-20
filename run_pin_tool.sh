cd tracer/
make
cd ..
cd scratch
pin -t ../tracer/obj-intel64/pintool.so -o dependency.txt -- ../mt_program/contension.out
cd ..