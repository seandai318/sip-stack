PROJECT_DIR = /home/ama/project
IDIR = ../include $(PROJECT_DIR)/os/include $(PROJECT_DIR)/sip-stack/codec/include $(PROJECT_DIR)/sip-stack/trans/include $(PROJECT_DIR)/sip-stack/transport/include  $(PROJECT_DIR)/sip-stack/tu/tuMgr/include $(PROJECT_DIR)/sip-stack/tu/proxy/proxyMgr/include $(PROJECT_DIR)/sip-stack/tu/registrar/include
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

libproxycall.a: $(obj)
	$(AR) -cr $@ $^

-include $(dep)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@mkdir -p $(dir $@)
	$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@


.PHONY: clean
clean:
	rm -f $(obj) *.a
	rm -f $(dep)

.PHONY: cleandep
cleandep:
	rm -f $(dep)
