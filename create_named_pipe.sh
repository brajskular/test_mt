#!/bin/bash

for (( index=0; index<=20; index++ ))
do
    mkfifo "named_pipe_${index}"
done
