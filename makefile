# PiJockey for RaspberryPi

TARGET=pj
VERSION=0.3

all:
	make -C src $@
	cp -fu src/$(TARGET) ./

clean:
	make -C src clean
	rm -f $(TARGET)

depend:
	make -C src depend

archive:
	hg archive -t tgz pj-$(VERSION)-src.tar.gz -p pj-$(VERSION)-src
