#!/bin/bash

TMPDIR=".grmon"
BAUD=460800

if [ -e $GRMON ]; then
#GRMON="/opt/grmon-eval/bin/grmon"
GRMON="/opt/grmon-eval-2.0.83/linux64/bin/grmon"
fi

if [ -z $2 ]; then
PORT="/dev/cobc_dsu_3"
else
PORT=$2
fi

mkdir -p $TMPDIR
echo "load $1
run
exit" > $TMPDIR/program.cfg
echo $GRMON -uart $PORT -stack 0x40ffffff -baud $BAUD -c $TMPDIR/program.cfg
$GRMON -uart $PORT -stack 0x40ffffff -baud $BAUD -c $TMPDIR/program.cfg
rm -rf $TMPDIR