
# Target(s) to build
TARGET_LIB = libprotorpc.a
TARGET_SRV = server
TARGET_CLN = client 
TARGET_SMT = servermt

# Sources
PROJECT_HOME = .
SRC_DIR = $(PROJECT_HOME)/src
OBJ_DIR = $(PROJECT_HOME)/_obj

SRCS_LIB = $(SRC_DIR)/rpc.cpp
SRCS_SRV = $(SRC_DIR)/server.cpp
SRCS_CLN = $(SRC_DIR)/client.cpp
SRCS_SMT = $(SRC_DIR)/servermt.cpp $(SRC_DIR)/threadPool.cpp

# Protobuf files 
PROTO_SRCS = $(SRC_DIR)/rpc.proto
PROTO_OUT  = $(PROJECT_HOME)/_gen

# Detect operating system
OS = $(shell uname -s)

# Protobuf support
ifeq "$(OS)" "Linux"
  PROTO_HOME = ../protobuf-3.5.1_install/linux
else ifeq "$(OS)" "SunOS"
  PROTO_HOME = ../protobuf-3.0.0_install/solaris
else
  PROTO_HOME = ../protobuf-3.5.1_install/mac
endif

PROTO_INC = $(PROTO_HOME)/include
PROTO_BIN = $(PROTO_HOME)/bin
PROTO_LIB = $(PROTO_HOME)/lib

PROTOC = $(PROTO_BIN)/protoc

# Include directories
INCS = -I$(PROTO_INC) \
       -I$(PROTO_OUT) 

# Libraries
LIBS = $(PROTO_LIB)/libprotobuf.a \
       $(PROJECT_HOME)/$(TARGET_LIB)

ifeq "$(OS)" "SunOS"
  LIBS += -lrpcsoc -lnsl -lrt -lresolv -lsocket
endif

# Protobuf files to generate from *.proto files 
PROTO_NAMES = $(basename $(notdir $(PROTO_SRCS)))
PROTO_CC    = $(addprefix $(PROTO_OUT)/, $(addsuffix .pb.cc, $(PROTO_NAMES)))
PROTO_OBJS  = $(addprefix $(OBJ_DIR)/,   $(addsuffix .pb.o,  $(PROTO_NAMES)))

# Objective files to build
OBJS_LIB = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_LIB)))))

OBJS_SRV =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_SRV)))))
OBJS_SRV += $(PROTO_OBJS)

OBJS_CLN =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_CLN)))))
OBJS_CLN += $(PROTO_OBJS)

OBJS_SMT =  $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS_SMT)))))
OBJS_SMT += $(PROTO_OBJS)

# Compiler and linker to use
ifeq "$(OS)" "Linux"
  CC = g++
  CFLAGS = -std=c++11 -D NDEBUG -O3 -Wall -fPIC
  LD = $(CC)
  LDFLAGS = 
  AR = ar
  ARFLAGS = rcs
else ifeq "$(OS)" "SunOS"
  CC = CC5.13
  CFLAGS = -std=c++11 -KPIC -m64 -D NDEBUG -O3
  LD = $(CC)
  LDFLAGS = -std=c++11 -KPIC -m64 -R'$$ORIGIN/src/solaris_libs' -L./src/solaris_libs
  AR = ar
  ARFLAGS = rcs
else 
  CC = g++
  CFLAGS = -std=c++11 -D NDEBUG -O3 -Wall -fPIC
  LD = $(CC)
  LDFLAGS = 
  AR = ar
  ARFLAGS = rcs
endif

# Build target(s)
ifeq "$(OS)" "Linux"
  all: $(TARGET_LIB) $(TARGET_SRV) $(TARGET_CLN) $(TARGET_SMT)
else ifeq "$(OS)" "SunOS"
  all: $(TARGET_LIB) $(TARGET_SRV) $(TARGET_CLN) $(TARGET_SMT)
else
  all: $(TARGET_LIB) $(TARGET_SRV) $(TARGET_CLN)
endif

$(TARGET_LIB): $(OBJS_LIB)
	$(AR) $(ARFLAGS) $(TARGET_LIB) $(OBJS_LIB)

$(TARGET_SRV): $(PROTO_CC) $(OBJS_SRV) $(TARGET_LIB)
	$(LD) $(LDFLAGS) -o $(TARGET_SRV) $(OBJS_SRV) $(LIBS)

$(TARGET_CLN): $(PROTO_CC) $(OBJS_CLN) $(TARGET_LIB)
	$(LD) $(LDFLAGS) -o $(TARGET_CLN) $(OBJS_CLN) $(LIBS)

$(TARGET_SMT): $(PROTO_CC) $(OBJS_SMT) $(TARGET_LIB)
	$(LD) $(LDFLAGS) -o $(TARGET_SMT) $(OBJS_SMT) $(LIBS) -pthread

# Compile source files
# Add -MP to generate dependency list
# Add -MMD to not include system headers
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp Makefile   
	-mkdir -p $(OBJ_DIR)
	$(CC) -c -MP -MMD $(CFLAGS) $(INCS) -o $(OBJ_DIR)/$*.o $<
	
# Compile Protobuf source files 
$(OBJ_DIR)/%.o: $(PROTO_OUT)/%.cc Makefile
	-mkdir -p $(OBJ_DIR)
	$(CC) -c $(CFLAGS) $(INCS) -I$(PROTO_OUT) -o $(OBJ_DIR)/$*.o $<

# Generate protobuf files
$(PROTO_OUT)/%.pb.cc: $(SRC_DIR)/%.proto Makefile
	@echo ">>> Generating proto files..."
	-mkdir -p $(PROTO_OUT)
	LD_LIBRARY_PATH=$(PROTO_LIB):$(LD_LIBRARY_PATH) $(PROTOC) --cpp_out=$(PROTO_OUT) --proto_path=$(SRC_DIR) $<

# Delete all intermediate files
clean: 
#	@echo PROTO_HOME = $(PROTO_HOME)
#	@echo PROTO_LIB = $(PROTO_LIB)
#	@echo PROTO_SRCS = $(PROTO_SRCS)
#	@echo PROTO_NAMES = $(PROTO_NAMES)
#	@echo PROTO_OUT = $(PROTO_OUT)
#	@echo PROTO_CC = $(PROTO_CC)
#	@echo PROTO_OBJS = $(PROTO_OBJS)
#	@echo OBJS = $(OBJS)
	rm -rf $(TARGET_LIB) $(TARGET_SRV) $(TARGET_CLN) $(TARGET_SMT) $(OBJ_DIR) $(PROTO_OUT)

#
# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
#
-include $(OBJS_LIB:.o=.d)
-include $(OBJS_SRV:.o=.d)
-include $(OBJS_CLN:.o=.d)
-include $(OBJS_SMT:.o=.d)


