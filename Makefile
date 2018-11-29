MAPNIK_INCLUDES=$(shell mapnik-config --includes)
MAPNIK_DEP_INCLUDES=$(shell mapnik-config --dep-includes)
MAPNIK_DEFINES=$(shell mapnik-config --defines)
MAPNIK_LIBS=$(shell mapnik-config --libs)
MAPNIK_DEP_LIBS=$(shell mapnik-config --dep-libs)

CXX=g++
CXXFLAGS=-std=c++11 -g $(MAPNIK_INCLUDES) $(MAPNIK_DEP_INCLUDES) $(MAPNIK_DEFINES)
LDFLAGS=$(MAPNIK_LIBS) $(MAPNIK_DEP_LIBS) -lpthread -lboost_program_options

SRCS=$(wildcard src/*.cpp)
OBJS=$(SRCS:.cpp=.o)

mapnik-render:$(OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@
