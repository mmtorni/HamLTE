CPPFLAGS=-Wall -g -O2
CXXFLAGS=-fPIC -std=gnu++17 -fvisibility=hidden
CFLAGS=-fPIC
LDFLAGS=-g -lefence

.PHONY : all

all: rlc_mux.so rlc_tm.so

%: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LOADLIBES) $(LDFLAGS) -lstdc++ $^ -o $@

%.so: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LOADLIBES) $(LDFLAGS) -shared -lstdc++ $^ -o $@

%.so: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOADLIBES) $(LDFLAGS) -shared $^ -o $@
