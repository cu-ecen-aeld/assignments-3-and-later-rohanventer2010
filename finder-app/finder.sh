#!/bin/sh
# Script for assignment 1
# Author: rohanventer2010

if [ $# -ne 2 ]
then
	echo "Usage: $0 <filesdir> <searchstr>"
	exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]
then
	echo "$filesdir should be a directory!"
	exit 1
fi
	
X=`grep -r -n -v "$searchstr" "$filesdir" | wc -l`
Y=`grep -r -n "$searchstr" "$filesdir" | wc -l`

X=$(($X + $Y))

echo "The number of files are $X and the number of matching lines are $Y"
