
# Include the Rules.mk file from the parent directory
include /home/ctnano/Desktop/jetson_multimedia_api/samples/Rules.mk

# Define the name of the application
APP := camera_jpeg_capture

# Define the directory where Argus utilities are located
# TOP_DIR 	:= $(shell pwd | awk '{split($$0, f, "/samples"); print f[1]}')

# ARGUS_UTILS_DIR := /home/ctnano/Desktop/jetson_multimedia_api/argus/samples/utils
# CLASS_DIR := /home/ctnano/Desktop/jetson_multimedia_api/samples/common/classes

ARGUS_UTILS_DIR := /usr/src/jetson_multimedia_api/argus/samples/utils
CLASS_DIR := /usr/src/jetson_multimedia_api/samples/common/classes


# Define the source files for the application
# CLASS_DIR 	:= $(TOP_DIR)/samples/common/classes

SRCS := \
	main.cpp \
	$(wildcard $(CLASS_DIR)/*.cpp) \
	$(ARGUS_UTILS_DIR)/Thread.cpp

# Define the object files to be generated from the source files
OBJS := $(SRCS:.cpp=.o)

# Add additional compiler flslags to include the Argus utilities directory
# Need to add include folder
CPPFLAGS += \
    -I"/usr/src/jetson_multimedia_api/argus/include" \
	-I"/usr/src/jetson_multimedia_api/include" \
	-I"$(ARGUS_UTILS_DIR)" 

# Add additional linker flags
LDFLAGS += \
	-lnveglstream_camconsumer -lnvargus_socketclient

# Define the default target (the application)
# all: $(APP)

# # Compile each class individually (this is a dependency target)
# $(CLASS_DIR)/%.o: $(CLASS_DIR)/%.cpp
# 	$(AT)$(MAKE) -C $(CLASS_DIR)

# Compile each source file into an object file
%.o: %.cpp
	@echo "Compiling: $<"
	$(CPP) $(CPPFLAGS) -c $< -o $@

# Link the object files to create the application
$(APP): $(OBJS)
	@echo "Linking: $@"
	$(CPP) -o $@ $(OBJS) $(CPPFLAGS) $(LDFLAGS)

# Clean up generated files
clean:
	$(AT)rm -rf $(APP) $(OBJS)