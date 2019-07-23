LIBFDT_DIR=../dtc/libfdt

include $(LIBFDT_DIR)/Makefile.libfdt

OBJ_FILES := dtbtool.o $(addprefix $(LIBFDT_DIR)/,$(LIBFDT_OBJS))
CFLAGS := -static -I$(LIBFDT_DIR) -O2 -fomit-frame-pointer -Wall

all: dtbTool

dtbTool: $(OBJ_FILES)
	$(CC) $(CFLAGS) -o $@ $^
	strip $@

clean:
	rm -f $(OBJ_FILES)
