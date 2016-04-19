#!/bin/sh

echo "make spiffs start ..."

rm -rf tmp
mkdir tmp
cp src/* tmp/
which html-minifier
if [ $? -eq 0 ]; then
	echo "try to minify html";
	html-minifier --minify-css --minify-js --collapse-boolean-attributes --collapse-whitespace --decode-entities --html-5 --process-conditional-comments --remove-attribute-quotes --remove-comments --remove-empty-attributes --remove-optional-tags --remove-redundant-attributes --remove-script-type-attributes --remove-style-link-type-attributes --remove-tag-whitespace --sort-attributes --sort-class-name --use-short-doctype -o tmp/index.html src/index.html
else
	echo "gzip without minified";
fi
gzip tmp/index.html
mkspiffs -c ./tmp -p 256 -b 8192 -s 1028096 spiffs.1m.bin

echo "make spiffs done"
