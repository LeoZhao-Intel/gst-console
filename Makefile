HW_ARCH=x86
DBG=true

ifeq ($(HW_ARCH),armv6)
        CC=/vobs/jem/cee4_lsp/mobilinux/devkit/arm/v6_vfp_le/bin/arm_v6_vfp_le-gcc
        AR=/vobs/jem/cee4_lsp/mobilinux/devkit/arm/v6_vfp_le/bin/arm_v6_vfp_le-ar
else
        CC=gcc
        AR=ar
endif

GST_INC := `pkg-config --cflags gstreamer-1.0 gstreamer-controller-1.0`
GST_LIBS := `pkg-config --libs --cflags gstreamer-1.0 gstreamer-controller-1.0`

CFLAGS= $(GST_INC) -Wall
LDFLAGS= ${GST_LIBS}

ifeq ($(DBG),false)
        CFLAGS += -O2
else
        CFLAGS += -g -pg
        LDFLAGS += -g
endif

all: gst-console

gst-console: control.o launch.o gstfunction.o
	libtool --mode=link $(CC) $(LDFLAGS) -o gst-console control.o launch.o gstfunction.o

control.o : control.c
	$(CC) ${CFLAGS} -c control.c -o control.o
launch.o : launch.c
	$(CC) ${CFLAGS} -c launch.c -o launch.o
gstfunction..o : gstfunction.c
	$(CC) ${CFLAGS} -c gstfunction..c -o gstfunction..o

clean: 
	-rm -f *.o gst-console
.PHONY: clean
