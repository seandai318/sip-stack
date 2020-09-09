PROJECT_DIR ?= ${HOME}/project
IDIR = ../include $(PROJECT_DIR)/os/include $(PROJECT_DIR)/sip-stack/codec/include $(PROJECT_DIR)/sip-stack/trans/include $(PROJECT_DIR)/sip-stack/tu/tuMgr/include $(PROJECT_DIR)/sip-stack/transport/include  $(PROJECT_DIR)/sip-stack/tu/registrar/include
#INC=$(foreach d, $(IDIR), -I$d)
INC=$(IDIR:%=-I%)

src = $(wildcard *.c)
obj = $(src:.c=.o)
dep = $(obj:.o=.d)  # one dependency file for each source

ifdef $(APP_BASE)
    include $(APP_BASE)/Makefile.cflags
    override CFLAGS += `mysql_config --cflags`
else
    CC=gcc
    AR=ar
    CFLAGS=$(INC) -g -DPREMEM -std=gnu99
    DEBUG = true
    ifeq ($(DEBUG), true)
        override CFLAGS += -DDEBUG -DPREMEM_DEBUG `mysql_config --cflags`
    endif
endif

LDFLAGS = -lpthread

libsms.a: $(obj)
	$(AR) -cr $@ $^

-include $(dep)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@mkdir -p $(dir $@)
	$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@


.PHONY: clean
clean:
	rm -f $(obj) *.o *.a
	rm -f $(dep)

.PHONY: cleandep
cleandep:
	rm -f $(dep)
