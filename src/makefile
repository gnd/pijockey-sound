
CC=gcc

CFLAGS =-Wall
CFLAGS+=-Wextra
CFLAGS+=-Wwrite-strings
CFLAGS+=-Wpointer-arith
CFLAGS+=-Wfloat-equal
CFLAGS+=-Wcast-align
ifeq (yes, $(DEBUG))
  CFLAGS+=-g
else
  CFLAGS+=-O2 -s
  #CFLAGS+=-DNDEBUG #prevent warning in firmware...
endif

INCLUDE =-I/opt/vc/include
INCLUDE+=-I/opt/vc/include/interface/vcos/pthreads
# vcos/pthreads is obsoluted. this path for old firmware.
INCLUDE+=-I/opt/vc/include/interface/vmcs_host/linux
LDFLAGS=-L/opt/vc/lib

LIBS =-lbcm_host
LIBS+=-lEGL
LIBS+=-lGLESv2
LIBS+=-lm
#LIBS+=-lopenmaxil
#LIBS+=-lvchostif -lvmcs_rpc_client -lvcfiled_check
#LIBS+=-lkhrn_static -lvchiq_arm -lrt -lpthread -lvcos

TARGET=pj

SOURCES =main.c
SOURCES+=pj.c
SOURCES+=video.c
SOURCES+=video_egl.c
SOURCES+=graphics.c

OBJECTS=$(subst .c,.o, $(SOURCES))


all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBS)

clean:
	rm -f *~
	rm -f $(OBJECTS) $(TARGET)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -c $<

gen: depend

depend:
	$(CC) -MM -w $(INCLUDE) $(SOURCES) > depend.inc

include depend.inc
