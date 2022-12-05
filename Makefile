src = $(wildcard *.c)
obj = $(src:.c=.o)

LIBNAME = libdenydevice
CC = /usr/bin/gcc
CFLAGS = -shared -fPIC
LDFLAGS = -ludev

ifeq ($(BITS),32)
	TARGET = $(LIBNAME)_32
	CFLAGS += -m32
else
	TARGET = $(LIBNAME)
endif

$(TARGET): $(obj)
	$(CC) $(CFLAGS) -o $(TARGET).so $^ $(LDFLAGS)

all:
	make cleanobj
	make BITS=32
	make cleanobj
	make
	make cleanobj

cleanobj:
	rm -f $(obj)

cleantarget:
	rm -f $(TARGET).so

clean:
	make cleanobj
	make cleantarget BITS=32
	make cleantarget

install:
	cp $(TARGET).so /usr/local/lib/$(TARGET).so
	cp $(TARGET)_32.so /usr/local/lib32/$(TARGET).so
