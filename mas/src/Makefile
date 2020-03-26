PROJECT_DIR = /home/ama/project
IDIR = $(PROJECT_DIR)/os/include $(PROJECT_DIR)/sip-stack/sip/codec/include $(PROJECT_DIR)/sip-stack/sip/trans/include $(PROJECT_DIR)/sip-stack/sip/transport/include $(PROJECT_DIR)/sip-stack/sip/tu/include $(PROJECT_DIR)/sip-stack/mas/include
#INC=$(foreach d, $(IDIR), -I$d)
INC=$(IDIR:%=-I%)
CC=gcc

src = $(wildcard *.c)
obj = $(src:.c=.o)
dep = $(obj:.o=.d)  # one dependency file for each source

CFLAGS=$(INC) -g -DPREMEM `mysql_config --cflags`
DEBUG = true
ifeq ($(DEBUG), true)
    override CFLAGS += -DDEBUG -DPREMEM_DEBUG 
endif

LDFLAGS = -L$(PROJECT_DIR)/os/src -L$(PROJECT_DIR)/sip-stack/sip/codec/src -L$(PROJECT_DIR)/sip-stack/sip/trans/src -L$(PROJECT_DIR)/sip-stack/sip/transport/src -L$(PROJECT_DIR)/sip-stack/sip/tu/src -los -lsip -ltransport -ltrans -ltu -lpthread -lrt `mysql_config --libs`

#libmas.a: $(obj)
#	$(AR) -cr $@ $^

mas: $(obj) libos libsip libtransport libtrans libtu
	$(CC) -o $@ -g $(filter %.o, $^) $(LDFLAGS)

-include $(dep)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@mkdir -p $(dir $@)
	$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

.PHONY: libos
libos:
	$(MAKE) -C $(PROJECT_DIR)/os/src

.PHONY: libsip
libsip:
	$(MAKE) -C $(PROJECT_DIR)/sip-stack/sip/codec/src

.PHONY: libtrans
libtrans:
	$(MAKE) -C $(PROJECT_DIR)/sip-stack/sip/trans/src

.PHONY: libtransport
libtransport:
	$(MAKE) -C $(PROJECT_DIR)/sip-stack/sip/transport/src

.PHONY: libtu
libtu:
	$(MAKE) -C $(PROJECT_DIR)/sip-stack/sip/tu/src



.PHONY: clean
clean: osclean sipclean transclean transportclean tuclean
	rm -f $(obj) sgsn
	rm -f $(dep)

.PHONY: osclean
osclean:
	cd $(PROJECT_DIR)/os/src; rm -f *.o *.a *.d; cd -

.PHONY: sipclean
sipclean:
	cd $(PROJECT_DIR)/sip-stack/sip/codec/src; rm -f *.o *.a *.d; cd -

.PHONY: transclean
transclean:
	cd $(PROJECT_DIR)/sip-stack/sip/trans/src; rm -f *.o *.a *.d; cd -

.PHONY: transportclean
transportclean:
	cd $(PROJECT_DIR)/sip-stack/sip/transport/src; rm -f *.o *.a *.d; cd -

.PHONY: tuclean
tuclean:
	cd $(PROJECT_DIR)/sip-stack/sip/tu/src; rm -f *.o *.a *.d; cd -

.PHONY: cleandep
cleandep:
	rm -f $(dep)
