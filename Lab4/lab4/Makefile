CC = gcc
CPP = g++
MKDEP=/usr/X11R6/bin/makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = 
  CFLAGS = -g -Wall -Wno-deprecated
else
  LIBS = -lssl -lcrypto
  CFLAGS = -g -Wall -Wno-deprecated
endif

BINS = dhtn
HDRS = netimg.h hash.h
SRCS = 
HDRS_SLN = dhtn.h
SRCS_SLN = dhtn.cpp hash.cpp
OBJS = $(SRCS_SLN:.cpp=.o) $(SRCS:.cpp=.o)

all: $(BINS)

dhtn: $(OBJS) $(HDRS)
	$(CPP) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp
	$(CPP) $(CFLAGS) $(INCLUDES) -c $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean: 
	-rm -f -r $(OBJS) *.o *~ *core* $(BINS)

depend: $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(SRCS) $(SRCS_SLN) $(HDRS) $(HDRS_SLN) >& /dev/null

altdepend: $(ALTSRCS_SLN) $(ALTHDRS) $(HDRS_SLN) Makefile
	$(MKDEP) $(CFLAGS) $(ALTSRCS_SLN) $(ALTHDRS) $(HDRS_SLN) >& /dev/null

# DO NOT DELETE

dhtn.o: netimg.h hash.h dhtn.h
hash.o: netimg.h hash.h
dhtn.o: hash.h
