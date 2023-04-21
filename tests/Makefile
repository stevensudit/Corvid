include .env

CXX_GCC = $(CXX_GCC_BIN)
CXX_GCCFLAGS = -std=c++20 -Wall -Wextra -g
CXX_CLANG = $(CXX_CLANG_BIN)
CXX_CLANGFLAGS = -std=c++20 -Wall -Wextra -g

SRCS := $(wildcard *.cpp)
GCC_EXES := $(addsuffix -gcc.exe, $(basename $(SRCS)))
CLANG_EXES := $(addsuffix -clang.exe, $(basename $(SRCS)))

.PHONY: all clean test

all: $(GCC_EXES) $(CLANG_EXES)

%-gcc.exe: %.cpp
	$(CXX_GCC) $(CXX_GCCFLAGS) $< -o $@

%-clang.exe: %.cpp
	$(CXX_CLANG) $(CXX_CLANGFLAGS) $< -o $@

test: $(GCC_EXES) $(CLANG_EXES)
	@for %%i in ($(GCC_EXES) $(CLANG_EXES)) do (\
		echo Running %%i &\
		( .\\%%i & if errorlevel 1 ( echo %%i failed & exit /b 1 ) else ( echo %%i passed ) )\
	)

clean:
	del /q $(GCC_EXES) $(CLANG_EXES)
  