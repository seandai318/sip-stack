PROJECT_DIR ?= ${HOME}/project
PROJECT_APP_DIR = $(PROJECT_DIR)/sip-stack/tu/cscf
SIPTU_DIR = $(PROJECT_DIR)/sip-stack/tu
DIA_DIR = $(PROJECT_DIR)/diameter
DNS_DIR = $(PROJECT_DIR)/dnsResolver
CSCF_DIR = $(PROJECT_DIR)/sip-stack/tu/cscf
SCSCF_DIR = $(PROJECT_DIR)/sip-stack/tu/cscf/scscf
#ICSCF_DIR = $(PROJECT_DIR)/sip-stack/tu/cscf/icscf
IDIR = ../include $(PROJECT_DIR)/os/include $(PROJECT_DIR)/sip-stack/codec/include $(PROJECT_DIR)/sip-stack/trans/include $(PROJECT_DIR)/sip-stack/tu/tuMgr/include $(PROJECT_DIR)/sip-stack/transport/include $(SIPTU_DIR)/proxy/proxyMgr/include $(DIA_DIR)/common/include $(DIA_DIR)/diaCx/include $(DNS_DIR)/include $(CSCF_DIR)/common/include $(SCSCF_DIR)/include
#INC=$(foreach d, $(IDIR), -I$d)
INC=$(IDIR:%=-I%)

SRC_DIR = $(PROJECT_APP_DIR)/common/src
OBJ_DIR = $(PROJECT_APP_DIR)/common/debug
src = $(wildcard $(SRC_DIR)/*.c)
obj = $(src:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
dep = $(obj:.o=.d)  # one dependency file for each source

ifdef $(APP_BASE)
    include $(APP_BASE)/Makefile.cflags
else
    CC=gcc
    AR=ar
    CFLAGS=$(INC) -g -DPREMEM -std=gnu99
    DEBUG = true
    ifeq ($(DEBUG), true)
        override CFLAGS += -DDEBUG -DPREMEM_DEBUG
    endif
endif

LDFLAGS = -lpthread

libcscfcommon.a: $(obj)
	cd $(OBJ_DIR);  $(AR) -cr $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	cd $(SRC_DIR); $(CC) $(CFLAGS) -c $(notdir $<) -o $@

-include $(dep)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
$(OBJ_DIR)/%.d: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@


.PHONY: clean
clean:
	rm -f $(dep) $(obj) $(OBJ_DIR)/*.a

.PHONY: cleandep
cleandep:
	rm -f $(dep)
