content: include src LICENSE

include:
	mkdir -p $@
	cp -r $(REP_DIR)/$@/* $@/

src:
	mkdir -p $@
	cp -r $(REP_DIR)/$@/* $@/

LICENSE:
	cp $(REP_DIR)/$@ $@
