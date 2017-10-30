
all: fastromfstool fstest

fastromfstool: fastromfstool.cpp ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	g++ -g -Wall -Wpedantic -o fastromfstool -DPROGMEM= -DDEBUGFASTROMFS=0 fastromfstool.cpp ../src/ESP8266FastROMFS.cpp -I ../src

fstest: ../examples/FSTest/FSTest.ino ../src/ESP8266FastROMFS.cpp ../src/ESP8266FastROMFS.h
	cp ../examples/FSTest/FSTest.ino ./fstest.cpp
	g++ -g -Wall -Wpedantic -o fstest -DPROGMEM= -DDEBUGFASTROMFS=1 fstest.cpp ../src/ESP8266FastROMFS.cpp -I ../src
	rm -f ./fstest.cpp

test: fstest
	valgrind --leak-check=full --show-leak-kinds=all ./fstest

clean:
	rm -f fastromfstool fstest
