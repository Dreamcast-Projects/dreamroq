# Put the filename of the output binary here
TARGET = dreamroq-player.elf

# List all of your C files here, but change the extension to ".o"
OBJS = main.o roq-player.o dreamroqlib.o romdisk.o

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS) romdisk.*

rm-elf:
	-rm -f $(TARGET) romdisk.*

# If you don't need a ROMDISK, then remove "romdisk.o" from the next few
# lines. Also change the -l arguments to include everything you need,
# such as -lmp3, etc.. these will need to go _before_ $(KOS_LIBS)
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

# You can safely remove the next two targets if you don't use a ROMDISK
romdisk.img:
	$(KOS_GENROMFS) -f romdisk.img -d romdisk -v

romdisk.o: romdisk.img
	$(KOS_BASE)/utils/bin2o/bin2o romdisk.img romdisk romdisk.o

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

runip: $(TARGET)
	$(KOS_IP_LOADER) $(TARGET)

rei: dist
	$(REICAST) GAME.CDI

lxd: dist
	$(LXDREAM) GAME.CDI

fly: dist
	$(FLYCAST) GAME.CDI

dist: $(TARGET)
	$(KOS_STRIP) $(TARGET)
	$(KOS_OBJCOPY) -O binary $(TARGET) prog.bin
	$(KOS_SCRAMBLE) prog.bin 1ST_READ.BIN
	mkdir -p ISO
	cp 1ST_READ.BIN ISO
	mkisofs -C 0,11702 -V DCTEST -G ${KOS_BASE}/IP.BIN -J -R -l -o GAME.ISO ISO
	$(CREATE_CDI) GAME.ISO GAME.CDI
	rm GAME.ISO
	rm 1ST_READ.BIN
	rm prog.bin

