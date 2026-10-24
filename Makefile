# Builds the mini shell:  make  ->  ./myshell
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
TARGET   = myshell
OBJS     = command.o tokenizer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

command.o: command.cc command.h tokenizer.h
	$(CXX) $(CXXFLAGS) -c command.cc

tokenizer.o: tokenizer.cc tokenizer.h
	$(CXX) $(CXXFLAGS) -c tokenizer.cc

clean:
	rm -f $(OBJS) $(TARGET) myshell.log

.PHONY: all clean
