content: src LICENSE

src:
	mkdir -p $@
	cp -r $(REP_DIR)/$@/* $@/

LICENSE:
	cp $(REP_DIR)/$@ $@
