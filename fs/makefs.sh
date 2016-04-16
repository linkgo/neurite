#!/bin/sh

echo "make spiffs start ..."

rm -rf tmp
mkdir tmp
cp src/* tmp/
gzip tmp/index.html
mkspiffs -c ./tmp -p 256 -b 8192 -s 1028096 spiffs.1m.bin

echo "make spiffs done"
