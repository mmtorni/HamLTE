CPPFLAGS=-Wall -g
CXXFLAGS=-fPIC -std=gnu++17
CFLAGS=-fPIC
LDFLAGS=-g

.PHONY : all

all: rlc_mux rlc_tm.so

%: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LOADLIBES) $(LDFLAGS) -lstdc++ $^ -o $@

%.so: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LOADLIBES) $(LDFLAGS) -shared -lstdc++ $^ -o $@

%.so: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOADLIBES) $(LDFLAGS) -shared $^ -o $@
