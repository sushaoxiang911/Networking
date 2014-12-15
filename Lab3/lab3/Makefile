CC = gcc
CPP = g++
MKDEP=/usr/X11R6/bin/makedepend -Y
OS := $(shell uname)
ifeq ($(OS), Darwin)
  LIBS = -framework OpenGL -framework GLUT -lc
  CFLAGS = -g -Wall -Wno-deprecated
else
  LIBS = -lGL -lGLU -lglut -lssl -lcrypto
  CFLAGS = -g -Wall -Wno-deprecated
endif

BINS = dhtn dhtc
HDRS = netimg.h ltga.h hash.h imgdb.h
SRCS = ltga.cpp netimg.cpp dhtn.cpp 
HDRS_SLN = 
SRCS_SLN = imgdb.cpp hash.cpp
OBJS = $(SRCS_SLN:.cpp=.o) $(SRCS:.cpp=.o)

all: $(BINS)

dhtn: $(OBJS) $(HDRS)
	$(CPP) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

dhtc: dhtc.o netimg.h netimg.o
	$(CPP) $(CFLAGS) -o $@ $< netimg.o $(LIBS)

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

ltga.o: ltga.h
netimg.o: netimg.h
dhtn.o: ltga.h netimg.h imgdb.h hash.h
imgdb.o: ltga.h netimg.h hash.h imgdb.h
hash.o: netimg.h hash.h
imgdb.o: ltga.h hash.h
