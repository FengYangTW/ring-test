EXEC = \
	ring-udp-echo \
	test-ring

OUT ?= .build
.PHONY: all
all: $(OUT) $(EXEC)

CC ?= gcc
CFLAGS = -std=gnu99 -Wall -O2 -g -I .
LDFLAGS = -lpthread

OBJS := \
	ring.o \
	
	
deps := $(OBJS:%.o=%.o.d)
OBJS := $(addprefix $(OUT)/,$(OBJS))
deps := $(addprefix $(OUT)/,$(deps))

test-ring: $(OBJS) test-ring.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ring-udp-echo: $(OBJS) ring-udp-echo.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	
$(OUT)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ -MMD -MF $@.d $<

$(OUT):
	@mkdir -p $@

clean:
	$(RM) $(EXEC) $(OBJS) $(deps)
	@rm -rf $(OUT)

-include $(deps)
