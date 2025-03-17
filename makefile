SDKNAME			=bella_scene_sdk
OUTNAME			=vmax2bella
UNAME           =$(shell uname)

ifeq ($(UNAME), Darwin)

	SDKBASE		= ../bella_scene_sdk

	SDKFNAME    = lib$(SDKNAME).dylib
	LZFSELIBNAME = liblzfse.dylib
	INCLUDEDIRS	= -I$(SDKBASE)/src
	INCLUDEDIRS2	= -I../lzfse/src
	LIBDIR		= $(SDKBASE)/lib
	LIBDIRS		= -L$(LIBDIR)
	LZFSEBUILDDIR = ../lzfse/build
	LZFSELIBDIR	= -L$(LZFSEBUILDDIR)
	OBJDIR		= obj/$(UNAME)
	BINDIR		= bin/$(UNAME)
	OUTPUT      = $(BINDIR)/$(OUTNAME)

	ISYSROOT	= /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk

	CC			= clang
	CXX			= clang++

	CCFLAGS		= -arch x86_64\
				  -arch arm64\
				  -mmacosx-version-min=11.0\
				  -isysroot $(ISYSROOT)\
				  -fvisibility=hidden\
				  -O3\
				  $(INCLUDEDIRS)\
				  $(INCLUDEDIRS2)

	CFLAGS		= $(CCFLAGS)\
				  -std=c17

	CXXFLAGS    = $(CCFLAGS)\
				  -std=c++17
				
	CPPDEFINES  = -DNDEBUG=1\
				  -DDL_USE_SHARED

	LIBS		= -l$(SDKNAME)\
				  -lm\
				  -ldl\
				  -llzfse

	LINKFLAGS   = -mmacosx-version-min=11.0\
				  -isysroot $(ISYSROOT)\
				  -framework Cocoa\
				  -framework IOKit\
				  -fvisibility=hidden\
				  -O5\
				  -rpath @executable_path
else

	SDKBASE		= ../bella_scene_sdk

	SDKFNAME    = lib$(SDKNAME).so
	SDKFNAME2   = liblzfse.so
	INCLUDEDIRS	= -I$(SDKBASE)/src
	INCLUDEDIRS2	= -I../lzfse/src
	LIBDIR		= $(SDKBASE)/lib
	LIBDIRS		= -L$(LIBDIR)
	OBJDIR		= obj/$(UNAME)
	BINDIR		= bin/$(UNAME)
	OUTPUT      = $(BINDIR)/$(OUTNAME)

	CC			= gcc
	CXX			= g++

	CCFLAGS		= -m64\
				  -Wall\
				  -fvisibility=hidden\
				  -D_FILE_OFFSET_BITS=64\
				  -O3\
				  $(INCLUDEDIRS)\
				  $(INCLUDEDIRS2)


	CFLAGS		= $(CCFLAGS)\
				  -std=c11

	CXXFLAGS    = $(CCFLAGS)\
				  -std=c++11
				
	CPPDEFINES  = -DNDEBUG=1\
				  -DDL_USE_SHARED

	LIBS		= -l$(SDKNAME)\
				  -lm\
				  -ldl\
				  -lrt\
				  -lpthread\
				  -llzfse

	LINKFLAGS   = -m64\
				  -fvisibility=hidden\
				  -O3\
				  -Wl,-rpath,'$$ORIGIN'\
				  -Wl,-rpath,'$$ORIGIN/lib'
endif

OBJS = vmax2bella.o
OBJ = $(patsubst %,$(OBJDIR)/%,$(OBJS))

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(CPPDEFINES)

$(OUTPUT): $(OBJ)
	@mkdir -p $(@D)
	$(CXX) -o $@ $^ $(LINKFLAGS) $(LIBDIRS) $(LZFSELIBDIR) $(LIBS)
	@cp $(LIBDIR)/$(SDKFNAME) $(BINDIR)/$(SDKFNAME)
	@cp $(LZFSEBUILDDIR)/$(LZFSELIBNAME) $(BINDIR)/$(LZFSELIBNAME)

.PHONY: clean
clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(OUTPUT)
	rm -f $(BINDIR)/$(SDKFNAME)
