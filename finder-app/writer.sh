#!/bin/sh
# Script for assignment 1
# Author: rohanventer2010

if [ $# -ne 2 ]
then
	echo "Usage: $0 <writefile> <writestr>"
	exit 1
fi

writefile=$1
writestr=$2

dirname=`dirname "$writefile"`
filename=`basename "$writefile"`

mkdir -p "$dirname"
if [ $? -ne 0 ]
then
	echo "File could not be created"
	exit 1
fi

echo "$writestr" > "$writefile"
if [ $? -ne 0 ]
then
	echo "File could not be created"
	exit 1
fi




