ifneq ($(KERNELRELEASE),)

# -------------------------------------------------------------------------------
#          Kbuild Part (invoked *inside* build/, after copy)
# -------------------------------------------------------------------------------
obj-m := eba.o
eba-y := ../src/eba.o ../src/eba_net.o ../src/eba_internals.o ../src/ebp.o ../src/eba_utils.o

ccflags-y += -I$(src)/../include

else

#===============================================================================
#  Outer Makefile
#===============================================================================
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean load remove lib debug-on debug-off

all:
	mkdir -p build
	cp Makefile build/Kbuild
	$(MAKE) -C $(KDIR) M=$(PWD)/build modules
	rm -f build/Kbuild

clean:
	rm -rf build lib

load:
	@echo "\033[1;36m[load]\033[0m"
	@sudo insmod build/eba.ko
	@dmesg | tail
	@lsmod | grep eba || echo "Module not loaded."

remove:
	@echo "\033[1;36m[remove]\033[0m"
	@sudo rmmod eba
	@dmesg | tail
	@lsmod | grep eba || echo "Module not loaded."

lib:
	@echo "Building user-space API library..."
	@mkdir -p lib
	$(CC) -O2 -Wall -fPIC -I$(PWD)/include -c src/eba_user.c -o lib/eba_user.o
	$(CC) -shared -o lib/libeba.so lib/eba_user.o
	@echo ""
	@echo "User library libeba.so created in ./lib."
	@echo "Compile apps with: gcc -o myapp myapp.c -I$(PWD)/include -L$(PWD)/lib -leba"

debug-on:
	@echo ">>> Enabling EBA_DBG logs"
	@sudo sh -c 'echo 1 > /sys/module/eba/parameters/eba_debug'

debug-off:
	@echo ">>> Disabling EBA_DBG logs"
	@sudo sh -c 'echo 0 > /sys/module/eba/parameters/eba_debug'

endif
