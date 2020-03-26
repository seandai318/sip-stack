PROJECT_DIR = /home/ama/project
IDIR = $(PROJECT_DIR)/os/include $(PROJECT_DIR)/sip-stack/sip/codec/include $(PROJECT_DIR)/sip-stack/sip/trans/include $(PROJECT_DIR)/sip-stack/sip/transport/include $(PROJECT_DIR)/sip-stack/sip/tu/include
#INC=$(foreach d, $(IDIR), -I$d)
INC=$(IDIR:%=-I%)
AR=ar

src = $(wildcard *.c)
obj = $(src:.c=.o)
dep = $(obj:.o=.d)  # one dependency file for each source

CFLAGS=$(INC) -g -DPREMEM
DEBUG = true
ifeq ($(DEBUG), true)
    override CFLAGS += -DDEBUG -DPREMEM_DEBUG
endif

LDFLAGS = -lpthread

libtu.a: $(obj)
	$(AR) -cr $@ $^

-include $(dep)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@mkdir -p $(dir $@)
	$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@


.PHONY: clean
clean:
	rm -f $(obj) sgsn
	rm -f $(dep)

.PHONY: cleandep
cleandep:
	rm -f $(dep)
