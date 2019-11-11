CC=g++
CFLAGS=-std=c++17 -O2 -Wall -Wextra -pedantic \
	   -I/opt/vc/include
LDFLAGS=-L/opt/vc/lib -lbcm_host -lvcos -lvchiq_arm
TARGET=pi-stats
DEST=/usr/local/bin

.PHONY: all install install-service install-logrot clean

default: $(TARGET)
all: install install-service install-logrot clean

OBJECT=$(TARGET).o

$(OBJECT): $(TARGET).cpp
	$(CC) $(CFLAGS) -c -o $@ $<
$(TARGET): $(OBJECT)
	$(CC) $^ -o $@ $(LDFLAGS)

install: $(TARGET)
	mkdir -p $(DEST)
	mv $< $(DEST)/$(TARGET)

install-service: $(TARGET)
	cp $(TARGET).service /etc/systemd/system

install-logrot: $(TARGET)
	cp $(TARGET).conf /etc/logrotate.d/pi-stats

clean:
	$(RM) *~ *.o $(TARGET)
