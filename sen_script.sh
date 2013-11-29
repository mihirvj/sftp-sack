WIN=1024
MSS=504

sleep 2

START=$(date +%s)
exec/sender 127.0.0.1 7735 test/sending.txt $WIN $MSS
END=$(date +%s)
DIFF1=$(( $END - $START ))
echo "1. $DIFF" >> time.out

sleep 2

START=$(date +%s)
exec/sender 127.0.0.1 7735 test/sending.txt $WIN $MSS
END=$(date +%s)
DIFF1=$(( $END - $START ))
echo "2. $DIFF" >> time.out

sleep 2

START=$(date +%s)
exec/sender 127.0.0.1 7735 test/sending.txt $WIN $MSS
END=$(date +%s)
DIFF1=$(( $END - $START ))
echo "3. $DIFF" >> time.out

sleep 2

START=$(date +%s)
exec/sender 127.0.0.1 7735 test/sending.txt $WIN $MSS
END=$(date +%s)
DIFF1=$(( $END - $START ))
echo "4. $DIFF" >> time.out
