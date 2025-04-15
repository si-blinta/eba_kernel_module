ifneq ($(KERNELRELEASE),)

# ------------------------------------------------------------------------------
#          Kbuild Part (invoked *inside* build/, after copy)
# ------------------------------------------------------------------------------
# The name of your module (final artifact: eba.ko)
obj-m := eba.o

# 	to add
#   eba-y := ../src/hello.o ../src/another.o

eba-y := ../src/eba.o ../src/eba_net.o ../src/eba_internals.o ../src/ebp.o ../src/eba_utils.o

# If you need custom headers from ../include/
# $(src) will expand to the directory of this Makefile (i.e. "build" after copy).
# So "../include" from build’s perspective is "build/../include" => "../include"
ccflags-y += -I$(src)/../include
#===============================================================================
#  Otherwise, we're in the "outer" part of the Makefile (the usual `make all`)
#===============================================================================
else

# Adjust for your specific kernel headers path if needed
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	mkdir -p build
	# Copy this single Makefile *into* build/ but name it Kbuild (or Makefile).
	# That way, "M=$(PWD)/build" sees a Makefile (Kbuild) in build/
	cp Makefile build/Kbuild
	# Now invoke the kernel build system:
	$(MAKE) -C $(KDIR) M=$(PWD)/build modules
	# Clean up: remove the temporary copy
	rm -f build/Kbuild
	find src ! -name '*.c' -type f -delete

clean:
	rm -rf build lib

# Load (insert) the module
load:
	@echo "\033[1;36m[load]\033[0m"
	@sudo insmod build/eba.ko
	@dmesg | tail
	@lsmod | grep eba || echo "Module not loaded."

# Remove (unload) the module
remove:
	@echo "\033[1;36m[remove]\033[0m"
	@sudo rmmod eba
	@dmesg | tail
	@lsmod | grep eba || echo "Module not loaded."
	
# Target to build the user-space library from eba_user.c and put it in lib/
lib: 
	@echo "Building user-space API library..."
	@mkdir -p lib
	$(CC) -O2 -Wall -fPIC -I$(PWD)/include -c src/eba_user.c -o lib/eba_user.o
	$(CC) -shared -o lib/libeba.so lib/eba_user.o
	@echo ""
	@echo "User library libeba.so created successfully in the lib folder."
	@echo "To compile your user application, use a command like:"
	@echo "   gcc -o myapp myapp.c -I$(PWD)/include -L$(PWD)/lib -leba"
	
endif
