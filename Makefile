# Put the filename of the output binary here
TARGET = dreamroq-player.elf

TARGET_BIN = dreamroq-player.bin

# Set compiler flags
#KOS_CFLAGS += -g -finstrument-functions

# List all of your C files here, but change the extension to ".o"
OBJS = main.o roq-player.o dreamroqlib.o #profiler.o 

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS) romdisk.*

rm-elf:
	-rm -f $(TARGET) romdisk.*

# If you don't need a ROMDISK, then remove "romdisk.o" from the next few
# lines. Also change the -l arguments to include everything you need,
# such as -lmp3, etc.. these will need to go _before_ $(KOS_LIBS)
$(TARGET): $(OBJS) romdisk.o
	$(KOS_CC) $(KOS_CFLAGS) $(KOS_LDFLAGS) -o $(TARGET) $(KOS_START) \
		$(OBJS) romdisk.o $(OBJEXTRA) $(KOS_LIBS) 

# You can safely remove the next two targets if you don't use a ROMDISK
romdisk.img:
	$(KOS_GENROMFS) -f romdisk.img -d romdisk -v

romdisk.o: romdisk.img
	$(KOS_BASE)/utils/bin2o/bin2o romdisk.img romdisk romdisk.o

profileip: $(TARGET)
	sudo /opt/toolchains/dc-utils/dc-tool-ip -c "." -t 192.168.1.137 -x $(TARGET)

profileser: $(TARGET)
	sudo /opt/toolchains/dc-utils/dc-tool-ser -c "." -t /dev/cu.usbserial-ABSCDWND -b 115200 -x $(TARGET)

dot: 
	$(KOS_UTILS)/pvtrace $(TARGET)

image: dot
	dot -Tjpg graph.dot -o graph.jpg
	
lxd: dist
	$(LXDREAM) GAME.CDI

rei: dist
	$(REICAST) GAME.CDI

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

runip: $(TARGET)
	$(KOS_IP_LOADER) $(TARGET)

dist: $(TARGET)
	$(KOS_STRIP) $(TARGET)
	$(KOS_OBJCOPY) -O binary $(TARGET) $(TARGET_BIN) 
	$(KOS_SCRAMBLE) $(TARGET_BIN) 1ST_READ.BIN
	mkdir -p ISO
	cp 1ST_READ.BIN ISO
	mkisofs -C 0,11702 -V DCTEST -G ${KOS_BASE}/IP.BIN -J -R -l -o GAME.ISO ISO
	$(CREATE_CDI) GAME.ISO GAME.CDI
	rm GAME.ISO
	rm 1ST_READ.BIN
	rm $(TARGET)

