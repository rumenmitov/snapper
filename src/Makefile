OBJS = utils.o tiny_sha.o

snapper: $(OBJS)
	$(CC) $(LDFLAGS) $(LDLIBS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

