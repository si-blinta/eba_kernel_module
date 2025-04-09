ifneq ($(KERNELRELEASE),)

# ------------------------------------------------------------------------------
#          Kbuild Part (invoked *inside* build/, after copy)
# ------------------------------------------------------------------------------
# The name of your module (final artifact: eba.ko)
obj-m := eba.o

# 	to add
#   eba-y := ../src/hello.o ../src/another.o

eba-y := ../src/eba.o ../src/eba_net.o ../src/eba_internals.o

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
	rm -rf build

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
	
endif
