Sample Commands
===============

ap mode fs operation
--------------------
curl -X DELETE 'http://192.168.1.23/edit?dir=/index.html.gz'
curl -X GET 'http://192.168.1.23/list?dir=/'
curl -F "file=@index.html.gz" http://192.168.1.23/edit
for file in `ls -A1`; do curl -F "file=@$PWD/$file" http://192.168.1.23/edit; done

512k (64k spiffs)
-----------------
mkspiffs -c ./data -p 256 -b 4096 -s 65536 spiffs.64k.bin
esptool -cd ck -cb 115200 -cp /dev/ttyUSB1 -ca 0x6b000 -cf spiffs.64k.bin

4m (1m spiffs)
--------------
mkspiffs -c ./data -p 256 -b 8192 -s 1028096 spiffs.1m.bin
esptool -cd ck -cb 115200 -cp /dev/ttyUSB1 -ca 0x300000 -cf spiffs.1m.bin

flash firmware
--------------
esptool -vv -cd ck -cb 115200 -cp /dev/ttyUSB1 -ca 0x00000 -cf neurite.cpp.bin

mqtt push ota
-------------
mqtt publish -h accrete.org -t /neuro/neurite-00016694/ota 'http://192.168.100.154:8080/firmware/esp.bin'

mqtt config
-----------
mqtt publish -h accrete.org -t /neuro/neurite-000c1632/config/ssid -m 'linkgo.io'
mqtt publish -h accrete.org -t /neuro/neurite-000c1632/config/psk -m 'ilovelinkgo'
mqtt publish -h accrete.org -t /neuro/neurite-000c1632/config/topic_from -m '/neuro/chatroom'
mqtt publish -h accrete.org -t /neuro/neurite-000c1632/config/topic_to -m '/neuro/chatroom'
mqtt publish -h accrete.org -t /neuro/neurite-000c1632/reboot -m "1"

