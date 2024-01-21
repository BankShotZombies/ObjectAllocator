#GCC=g++
GCCFLAGS=-O -Werror -Wall -Wextra -Wconversion -std=c++14 -pedantic -Wold-style-cast

PRG=gnu.exe

OBJECTS0=ObjectAllocator.cpp PRNG.cpp
DRIVER0=driver.cpp

VALGRIND_OPTIONS=-q --leak-check=full
DIFF_OPTIONS=-y --strip-trailing-cr --suppress-common-lines -b

OSTYPE := $(shell uname)
ifeq ($(OSTYPE),Linux)
CYGWIN=
else
CYGWIN=-Wl,--enable-auto-import
endif

gcc0:
	g++ -o $(PRG) $(CYGWIN) $(DRIVER0) $(OBJECTS0) $(GCCFLAGS)
gcc1:
	clang++ -o $(PRG) $(CYGWIN) $(DRIVER0) $(OBJECTS0) $(GCCFLAGS)
gcc2:
	g++ -o $(PRG) $(CYGWIN) $(DRIVER0) $(OBJECTS0) $(GCCFLAGS) -m32
00:
	#echo "running test$@"
	#@echo "should run in less than 200 ms"
	#watchdog 200 ./$(PRG) input.txt >studentout$@
	#diff output$@.txt studentout$@ $(DIFF_OPTIONS) > difference$@
clean : 
	rm *.exe student* difference*
