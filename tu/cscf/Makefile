PROJECT_DIR ?= ${HOME}/project
PROJECT_APP_DIR = $(PROJECT_DIR)/sip-stack/tu/cscf
#IDIR = ../include $(PROJECT_DIR)/os/include $(PROJECT_APP_DIR)/common/include $(PROJECT_APP_DIR)/diaBase/include $(PROJECT_APP_DIR)/diaCx/include
#INC=$(foreach d, $(IDIR), -I$d)
#INC=$(IDIR:%=-I%)

src = $(wildcard *.c)
obj = $(src:.c=.o)
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

libcscf.a: libcscfcommon.a libscscf.a libicscf.a
	@mkdir -p $(PROJECT_APP_DIR)/bin-debug
	$(AR) -cr $@ $(PROJECT_APP_DIR)/bin-debug/*.o
#	$(AR) -cr $@ $^
#	rm -rf $^
#	@rm -rf $(PROJECT_APP_DIR)/bin-debug

-include $(dep)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@mkdir -p $(dir $@)
	$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

.PHONY: libcscfcommon.a
libcscfcommon.a:
	@mkdir -p $(PROJECT_APP_DIR)/bin-debug
	cd $(PROJECT_APP_DIR)/common; $(MAKE); cd debug; cp $@ $(PROJECT_APP_DIR)/bin-debug; cd $(PROJECT_APP_DIR)/bin-debug; $(AR) -x $@

.PHONY: libscscf.a
libscscf.a:
	@mkdir -p $(PROJECT_APP_DIR)/bin-debug
	cd $(PROJECT_APP_DIR)/scscf; $(MAKE); cd debug; cp $@ $(PROJECT_APP_DIR)/bin-debug; cd $(PROJECT_APP_DIR)/bin-debug; $(AR) -x $@

.PHONY: libicscf.a
libicscf.a:
	@mkdir -p $(PROJECT_APP_DIR)/bin-debug
	cd $(PROJECT_APP_DIR)/icscf; $(MAKE); cd debug; cp $@ $(PROJECT_APP_DIR)/bin-debug; cd $(PROJECT_APP_DIR)/bin-debug; $(AR) -x $@

.PHONY: clean
clean: cscfcommonclean scscfclean icscfclean
	rm -f $(dep)
	rm -rf $(PROJECT_APP_DIR)/bin-debug
	rm -rf *.a

.PHONY: cscfcommonclean
cscfcommonclean:
	cd $(PROJECT_APP_DIR)/common/debug; rm -f *.o *.a *.d; cd -

.PHONY: scscfclean
scscfclean:
	cd $(PROJECT_APP_DIR)/scscf/debug; rm -f *.o *.a *.d; cd -

.PHONY: icscfclean
icscfclean:
	cd $(PROJECT_APP_DIR)/scscf/debug; rm -f *.o *.a *.d; cd -

.PHONY: cleandep
cleandep:
	rm -f $(dep)
