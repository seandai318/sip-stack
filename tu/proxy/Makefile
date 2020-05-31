PROJECT_DIR = /home/ama/project
PROJECT_APP_DIR = $(PROJECT_DIR)/sip-stack/tu/proxy


libproxy.a: libproxycall.a libproxymgr.a
	@mkdir -p $(PROJECT_APP_DIR)/bin-debug
	$(AR) -cr $@ $(PROJECT_APP_DIR)/bin-debug/*.o


.PHONY: libproxycall.a
libproxycall.a:
	@mkdir -p $(PROJECT_APP_DIR)/bin-debug
	cd $(PROJECT_APP_DIR)/callProxy; $(MAKE); cd debug; cp $@ $(PROJECT_APP_DIR)/bin-debug; cd $(PROJECT_APP_DIR)/bin-debug; $(AR) -x $@

.PHONY: libproxymgr.a
libproxymgr.a:
	@mkdir -p $(PROJECT_APP_DIR)/bin-debug
	cd $(PROJECT_APP_DIR)/proxyMgr; $(MAKE); cd debug; cp $@ $(PROJECT_APP_DIR)/bin-debug; cd $(PROJECT_APP_DIR)/bin-debug; $(AR) -x $@


.PHONY: clean
clean: proxycallclean proxymgrclean
	rm -f $(PROJECT_APP_DIR)/bin-debug/*

.PHONY: proxycallclean
proxycallclean:
	cd $(PROJECT_APP_DIR)/callProxy/debug; rm -f *.o *.a *.d; cd -

.PHONY: proxymgrclean
proxymgrclean:
	cd $(PROJECT_APP_DIR)/proxyMgr/debug; rm -f *.o *.a *.d; cd -


.PHONY: cleandep
cleandep:
	rm -f $(dep)
