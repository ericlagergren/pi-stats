CC=g++
CFLAGS=-std=c++17 -O2 -Wall -Wextra -pedantic \
	   -I/opt/vc/include
LDFLAGS=-L/opt/vc/lib -lbcm_host -lvcos -lvchiq_arm
TARGET=rpi-stats
DEST=$(HOME)/.local/bin

.PHONY: all install clean

all: $(TARGET)

OBJECT=$(TARGET).o

$(OBJECT): $(TARGET).cpp
	$(CC) $(CFLAGS) -c -o $@ $<
$(TARGET): $(OBJECT)
	$(CC) $^ -o $@ $(LDFLAGS)

install: $(TARGET)
	mkdir -p $(DEST)
	cp $< $(DEST)/$(TARGET)

clean:
	$(RM) *~ *.o $(TARGET)
