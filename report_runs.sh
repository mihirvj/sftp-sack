START=$(date +%s)
# do something
# start your script work here
make clean
make
exec/receiver 7735 test/received.txt 0.5 &
exec/sender 127.0.0.1 7735 test/sending.txt 256 28
# your logic ends here
END=$(date +%s)
DIFF=$(( $END - $START ))
echo "$DIFF" >> time.out
