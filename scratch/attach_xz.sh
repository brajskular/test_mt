#!/bin/bash

for (( index=0; index<=20; index++ ))
do
    xz -0 -c "named_pipe_${index}" > compressed_${index}.xz &
done