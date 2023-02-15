# Prepare

Download [Intel Pin](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html) and unzip.

Use Pin 3.26.

Compile the program to be traced with the `-g` flag to include symbols.

# To Compile

```
$export PIN_ROOT=<path to Intel Pin directory>
$make
```

`./tracer/obj-intel64/champsim_mt_tracer.so` is the compiled tracer.

# To Run

```
$<path to Intel Pin directory>/pin -t obj-intel64/champsim_tracer.so -o <output file name> -- <script to start program>
```

