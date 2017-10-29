
all: mkfastromfs lsfsimage copytofsimage copyfromfsimage


mkfastromfs: mkfastromfs.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o mkfastromfs -DDEBUGFASTROMFS=0 mkfastromfs.cpp ../src/ESP8266FastROMFS.cpp -I ../src

lsfsimage: lsfsimage.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o lsfsimage -DDEBUGFASTROMFS=0 lsfsimage.cpp ../src/ESP8266FastROMFS.cpp -I ../src

copytofsimage: copytofsimage.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o copytofsimage -DDEBUGFASTROMFS=0 copytofsimage.cpp ../src/ESP8266FastROMFS.cpp -I ../src

copyfromfsimage: copyfromfsimage.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o copyfromfsimage -DDEBUGFASTROMFS=0 copyfromfsimage.cpp ../src/ESP8266FastROMFS.cpp -I ../src
