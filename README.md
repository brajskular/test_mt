# Prepare

Download Intel Pin and unzip.

Use Pin 3.26.

Previous versions of Pin do not support C++ 11 features.

# Test Program

Test program is in `./mt_program`.

Use `g++ -std=c++11 -pthread contension.cpp -o contension.out` to compile.

Use `./contension.out` to run.

# Pintool

To Compile:

```
export PIN_ROOT=<path to Intel Pin directory>
make
```

`./tracer/obj-intel64/champsim_mt_tracer.so` is the compiled pintool used for tracing.

To trace:

```
export PIN_ROOT=<path to Intel Pin directory>
$PIN_ROOT/pin -t obj-intel64/champsim_tracer.so -o <output file name> -- <script to start program>
```

