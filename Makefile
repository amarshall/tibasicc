# tibasicc - a compiler for TI-BASIC code 
# See LICENSE file for copyright and license details.  

VERSION = 0.1

# paths
PREFIX = /usr

PROJDIRS := src
SRCFILES := $(shell find $(PROJDIRS) -type f -name "*.cpp")
OBJFILES := $(patsubst %.cpp,%.o,$(SRCFILES))
DEPFILES := $(patsubst %.cpp,%.d,$(SRCFILES))

.PHONY: all clean

CXXFLAGS := -g -Wall -pedantic -O -D_FORTIFY_SOURCE=2

CXX := g++

all: tibasicc

tibasicc: $(OBJFILES)
	g++ -o tibasicc $(OBJFILES)

-include $(DEPFILES)

%.o: %.c
	@$(CXX) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
		rm -f $(OBJFILES)

install: all
	mkdir -p ${PREFIX}/bin
	install tibasicc ${PREFIX}/bin/

uninstall:
	rm -f ${PREFIX}/bin/tibasicc

.PHONY: all clean install uninstall
