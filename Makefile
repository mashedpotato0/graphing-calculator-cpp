CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I src/
SRCS     = src/lexer.cpp src/parser.cpp src/integrator.cpp src/math_editor.cpp
TARGET   = calc
REPL     = repl
VIEWER   = viewer

GTK_FLAGS = $(shell pkg-config --cflags --libs gtk4)

all: $(TARGET) $(REPL) $(VIEWER)

$(TARGET): $(SRCS) src/main.cpp src/ast.hpp src/ast_ext.hpp
	$(CXX) $(CXXFLAGS) $(SRCS) src/main.cpp -o $@ $(GTK_FLAGS)

$(REPL): $(SRCS) src/main_repl.cpp src/ast.hpp src/ast_ext.hpp
	$(CXX) $(CXXFLAGS) $(SRCS) src/main_repl.cpp -o $@ -lreadline $(GTK_FLAGS)

$(VIEWER): $(SRCS) src/viewer.cpp src/renderer.hpp src/ast.hpp src/ast_ext.hpp
	$(CXX) $(CXXFLAGS) $(SRCS) src/viewer.cpp -o $@ $(GTK_FLAGS)

clean:
	rm -f $(TARGET) $(REPL) $(VIEWER)

.PHONY: all clean
