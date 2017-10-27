
all: makefsimage lsfsimage copytofsimage copyfromfsimage


makefsimage: makefsimage.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o makefsimage -DDEBUGFASTROMFS=1 makefsimage.cpp ../src/ESP8266FastROMFS.cpp -I ../src

lsfsimage: lsfsimage.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o lsfsimage -DDEBUGFASTROMFS=1 lsfsimage.cpp ../src/ESP8266FastROMFS.cpp -I ../src

copytofsimage: copytofsimage.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o copytofsimage -DDEBUGFASTROMFS=1 copytofsimage.cpp ../src/ESP8266FastROMFS.cpp -I ../src

copyfromfsimage: copyfromfsimage.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -o copyfromfsimage -DDEBUGFASTROMFS=1 copyfromfsimage.cpp ../src/ESP8266FastROMFS.cpp -I ../src
