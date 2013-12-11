#!/usr/bin/env bash
if [ $# -lt 4 ]; then 
  echo "$0 origin ver1 ver2 output" 
  exit 1 
fi 

array=($@) 
for (( i = 0; i < 3; i = i + 1 )); do
  if [ ! -f ${array[$i]} ]; then 
    echo "no found ${array[$i]}" 
    exit 1 
  fi 
done 
cat $2 $3 > $4 
exit $?
