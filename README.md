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

Change `PATH` and `PIN_ROOT` environmental variables in `./env.sh` based on where Intel Pin is located.

# Scripts

Always run `source env.sh` before compiling or running the pintool.

Change directory to `./scratch`.

Run 

```
./create_named_pipe.sh
./attach_xz.sh
(./clean_trace cleans the named pipes. Do not clean the named pipes before tracing.)
```

Change directory back to `./`

Run

```
./run_pin_tool.sh
```

Files in `./scratch` end with `.xz` are the Champsim trace files.

`./scratch/dependency.txt` includes information on the ordering and dependency of threads which is printed out in the terminal while tracing.
