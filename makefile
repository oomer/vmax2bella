# Project configuration
BELLA_SDK_NAME    = bella_scene_sdk
EXECUTABLE_NAME   = vmax2bella
PLATFORM          = $(shell uname)

# Common paths
BELLA_SDK_PATH    = ../bella_scene_sdk
LZFSE_PATH        = ../lzfse
LIBPLIST_PATH     = ../libplist
OBJ_DIR           = obj/$(PLATFORM)
BIN_DIR           = bin/$(PLATFORM)
OUTPUT_FILE       = $(BIN_DIR)/$(EXECUTABLE_NAME)

# Platform-specific configuration
ifeq ($(PLATFORM), Darwin)
    # macOS configuration
    SDK_LIB_EXT          = dylib
    LZFSE_LIB_NAME       = liblzfse.$(SDK_LIB_EXT)
    PLIST_LIB_NAME       = libplist-2.0.4.$(SDK_LIB_EXT)
    MACOS_SDK_PATH       = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    
    # Compiler settings
    CC                   = clang
    CXX                  = clang++
    
    # Architecture flags
    ARCH_FLAGS           = -arch arm64 -mmacosx-version-min=11.0 -isysroot $(MACOS_SDK_PATH)
    
    # Linking flags - Use multiple rpath entries to look in executable directory
    LINKER_FLAGS         = $(ARCH_FLAGS) -framework Cocoa -framework IOKit -fvisibility=hidden -O5 \
                          -rpath @executable_path \
                          -rpath . 

                          #-rpath @loader_path \
                          #-Xlinker -rpath -Xlinker @executable_path
    
    # Platform-specific libraries
    PLIST_LIB            = -lplist-2.0
    
else
    # Linux configuration
    SDK_LIB_EXT          = so
    LZFSE_LIB_NAME       = liblzfse.$(SDK_LIB_EXT)
    PLIST_LIB_NAME       = libplist.$(SDK_LIB_EXT)
    
    # Compiler settings
    CC                   = gcc
    CXX                  = g++
    
    # Architecture flags
    ARCH_FLAGS           = -m64 -D_FILE_OFFSET_BITS=64
    
    # Linking flags
    LINKER_FLAGS         = $(ARCH_FLAGS) -fvisibility=hidden -O3 -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/lib'
    
    # Platform-specific libraries
    PLIST_LIB            = -lplist
endif

# Common include and library paths
INCLUDE_PATHS      = -I$(BELLA_SDK_PATH)/src -I$(LZFSE_PATH)/src -I$(LIBPLIST_PATH)/include
SDK_LIB_PATH       = $(BELLA_SDK_PATH)/lib
SDK_LIB_FILE       = lib$(BELLA_SDK_NAME).$(SDK_LIB_EXT)
LZFSE_BUILD_DIR    = $(LZFSE_PATH)/build
PLIST_LIB_DIR      = $(LIBPLIST_PATH)/src/.libs

# Library flags
LIB_PATHS          = -L$(SDK_LIB_PATH) -L$(LZFSE_BUILD_DIR) -L$(PLIST_LIB_DIR)
LIBRARIES          = -l$(BELLA_SDK_NAME) -lm -ldl -llzfse $(PLIST_LIB)

# Common compiler flags
COMMON_FLAGS       = $(ARCH_FLAGS) -fvisibility=hidden -O3 $(INCLUDE_PATHS)

# Language-specific flags
C_FLAGS            = $(COMMON_FLAGS) -std=c17
CXX_FLAGS          = $(COMMON_FLAGS) -std=c++17 -Wno-deprecated-declarations
CPP_DEFINES        = -DNDEBUG=1 -DDL_USE_SHARED

# Objects
OBJECTS            = vmax2bella.o
OBJECT_FILES       = $(patsubst %,$(OBJ_DIR)/%,$(OBJECTS))

# Build rules
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) -c -o $@ $< $(CXX_FLAGS) $(CPP_DEFINES)

$(OUTPUT_FILE): $(OBJECT_FILES)
	@mkdir -p $(@D)
	$(CXX) -o $@ $(OBJECT_FILES) $(LINKER_FLAGS) $(LIB_PATHS) $(LIBRARIES)
	@echo "Copying libraries to $(BIN_DIR)..."
	@cp $(SDK_LIB_PATH)/$(SDK_LIB_FILE) $(BIN_DIR)/$(SDK_LIB_FILE)
	@cp $(LZFSE_BUILD_DIR)/$(LZFSE_LIB_NAME) $(BIN_DIR)/$(LZFSE_LIB_NAME)
	@cp $(PLIST_LIB_DIR)/$(PLIST_LIB_NAME) $(BIN_DIR)/
	@echo "Build complete: $(OUTPUT_FILE)"

.PHONY: clean
clean:
	rm -f $(OBJ_DIR)/*.o
	rm -f $(OUTPUT_FILE)
	rm -f $(BIN_DIR)/$(SDK_LIB_FILE)
	rm -f $(BIN_DIR)/*.dylib
