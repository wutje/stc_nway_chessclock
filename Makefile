SDCC ?= sdcc
STCCODESIZE ?= 4089
SDCCOPTS ?= --code-size $(STCCODESIZE) --xram-size 0 --data-loc 0x30 --disable-warning 126 --disable-warning 59
SDCCREV ?= -Dstc15w404as 
STCGAL ?= stcgal/stcgal.py
STCGALOPTS ?= 
STCGALPORT ?= /dev/ttyUSB0
STCGALPROT ?= stc15
FLASHFILE ?= main.hex
SYSCLK ?= 11059
CFLAGS ?= -DFOSC=$(SYSCLK)200 -D WITH_ALT_LED9 -D WITHOUT_LEDTABLE_RELOC 
SRC = src/uart.c src/adc.c src/timer0.c

OBJ=$(patsubst src%.c,build%.rel, $(SRC))

all: main

build/%.rel: src/%.c src/%.h
	mkdir -p $(dir $@)
	$(SDCC) $(SDCCOPTS) $(SDCCREV) $(CFLAGS) -o $@ -c $<

main: $(OBJ)
	$(SDCC) -o build/ src/$@.c $(SDCCOPTS) $(SDCCREV) $(CFLAGS) $^
	@ tail -n 5 build/main.mem | head -n 2
	@ tail -n 1 build/main.mem
	cp build/$@.ihx $@.hex

eeprom:
	sed -ne '/:..1/ { s/1/0/2; p }' main.hex > eeprom.hex

flash: main
	#$(STCGAL) -p $(STCGALPORT) -P $(STCGALPROT) -t $(SYSCLK) $(STCGALOPTS) $(FLASHFILE)
	$(STCGAL) -p $(STCGALPORT) -P $(STCGALPROT) $(STCGALOPTS) $(FLASHFILE)

clean:
	rm -f *.ihx *.hex *.bin
	rm -rf build/*

cpp: SDCCOPTS+=-E
cpp: main
