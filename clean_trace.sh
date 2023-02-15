# find -type f -name "*Symbols*" -delete
# find -type f -name "*Trace*" -delete
# find -type f -name "*SystemCall*" -delete
# find -type f -name "*named_pipe*" -delete

for (( index=0; index<=20; index++ ))
do
    rm "named_pipe_${index}"
done


rm trace.Dependency
