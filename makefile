# Makefile for Orion-X3/Orion-X4/mx and derivatives
# Written in 2011
# This makefile is licensed under the WTFPL

X_BUILD_FILE	= .build.h

export QEMU			:= /usr/local/bin/qemu-system-x86_64
export SYSROOT		:= $(CURDIR)/build/sysroot
export TOOLCHAIN	:= $(CURDIR)/build/toolchain
export CXX			:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-g++
export AS			:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-as
export LD			:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-ld
export OBJCOPY		:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-objcopy
export READELF		:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-readelf
export STRIP		:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-strip
export AR			:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-ar
export RANLIB		:= $(CURDIR)/build/toolchain/bin/x86_64-orionx-ranlib
export MOUNTPATH	:= $(shell tools/getpath.sh)

# we use clang only for the kernel, don't pollute makefiles
CXX_				= clang++
GCCVERSION			= 4.9.1

WARNINGS			= -Wno-padded -Wno-c++98-compat-pedantic -Wno-c++98-compat -Wno-cast-align -Wno-unreachable-code -Wno-gnu -Wno-missing-prototypes -Wno-switch-enum -Wno-packed -Wno-missing-noreturn -Wno-float-equal -Wno-sign-conversion -Wno-old-style-cast -Wno-exit-time-destructors

CXXFLAGS			= -m64 -Weverything -msse3 -g -integrated-as -O2 -fPIC -std=gnu++11 -ffreestanding -mno-red-zone -fno-exceptions -fno-rtti  -I./source/Kernel/HeaderFiles -I./Libraries/Iris/HeaderFiles -I./Libraries/ -I$(SYSROOT)/usr/include -I$(SYSROOT)/usr/include/c++ -DORION_KERNEL=1 -target x86_64-elf -c

LDFLAGS				= --gc-sections -z max-page-size=0x1000 -T link.ld -L$(SYSROOT)/usr/lib


MEMORY				= 1024


SSRC				= $(shell find source -iname "*.s")
CXXSRC				= $(shell find source -iname "*.cpp")

SOBJ				= $(SSRC:.s=.s.o)
CXXOBJ				= $(CXXSRC:.cpp=.cpp.o)

CXXDEPS				= $(CXXOBJ:.o=.d)

.DEFAULT_GOAL = all
-include $(CXXDEPS)



NUMFILES			= $$(($(words $(CXXSRC)) + $(words $(SSRC))))



LIBRARIES			= -liris -lsupc++ -lgcc -lrdestl
OUTPUT				= build/kernel.mxa


.PHONY: builduserspace buildlib mountdisk clean all cleandisk copyheader

run:
	@$(QEMU) -s -vga std -serial file:"build/serialout.log" -no-reboot -m $(MEMORY) -hda build/disk.img -rtc base=utc -net nic,model=rtl8139 -net user -net dump,file=build/netdump.wcap -monitor stdio

all: $(OUTPUT)
	@# unmount??
	@tools/unmountdisk.sh

	@echo "# Starting QEMU"
	@$(QEMU) -s -vga std -serial file:"build/serialout.log" -no-reboot -m $(MEMORY) -hda build/disk.img -rtc base=utc -net nic,model=rtl8139 -net user -net dump,file=build/netdump.wcap
	-@rm -f build/.dmf

	@# mount the disk again for inspection.
	@tools/mountdisk.sh

build: $(OUTPUT)
	# built

$(OUTPUT): mountdisk copyheader $(SYSROOT)/usr/lib/%.a $(SOBJ) $(CXXOBJ) builduserspace
	@echo "\n# Linking object files"
	@$(LD) $(LDFLAGS) -o build/kernel64.elf source/Kernel/Boot/Start.s.o $(shell find source -name "*.o" ! -name "Start.s.o") $(LIBRARIES)

	@echo "# Performing objcopy"
	@$(OBJCOPY) -g -O elf32-i386 build/kernel64.elf build/kernel.mxa
	@cp $(OUTPUT) $(shell tools/getpath.sh)/boot/kernel.mxa


%.s.o: %.s
	@if [ ! -a build.dmf ]; then tools/updatebuild.sh; fi;
	@touch build/.dmf

	@$(AS) $< -o $@

	@$(eval DONEFILES += "S")
	@printf "\r                                               \r$(words $(DONEFILES)) / $(NUMFILES) ($(notdir $<))"


%.cpp.o: %.cpp
	@if [ ! -a build.dmf ]; then tools/updatebuild.sh; fi;
	@touch build/.dmf

	@$(CXX_) $(CXXFLAGS) $(WARNINGS) -MMD -MP -o $@ $<

	@$(eval DONEFILES += "CPP")
	@printf "\r                                               \r$(words $(DONEFILES)) / $(NUMFILES) ($(notdir $<))"

builduserspace:
	@printf "\n# Building userspace applications"
	@$(MAKE) -C applications/

copyheader:
	@mkdir -p $(SYSROOT)/usr/lib
	@mkdir -p $(SYSROOT)/usr/include/c++
	@rsync -cmar Libraries/libc/include/* $(SYSROOT)/usr/include/
	@rsync -cmar Libraries/libm/include/* $(SYSROOT)/usr/include/
	@rsync -cmar Libraries/Iris/HeaderFiles/* $(SYSROOT)/usr/include/iris/
	@rsync -cmar Libraries/libsyscall/*.h $(SYSROOT)/usr/include/sys/
	@rsync -cmar $(TOOLCHAIN)/x86_64-orionx/include/c++/$(GCCVERSION)/* $(SYSROOT)/usr/include/c++/
	@rsync -cmar $(SYSROOT)/usr/include/c++/x86_64-orionx/bits/* $(SYSROOT)/usr/include/c++/bits/
	@cp $(TOOLCHAIN)/x86_64-orionx/lib/*.a $(SYSROOT)/usr/lib/
	@cp $(TOOLCHAIN)/lib/gcc/x86_64-orionx/4.9.1/libgcc.a $(SYSROOT)/usr/lib/


buildlib: $(SYSROOT)/usr/lib/%.a
	@:

$(SYSROOT)/usr/lib/%.a:
	@echo "# Building Libraries"
	@$(MAKE) -C Libraries/

mountdisk:
	@tools/mountdisk.sh

cleandisk:
	@find $(MOUNTPATH) -name "*.mxa" | xargs rm -f
	@find $(MOUNTPATH) -name "*.x" | xargs rm -f

cleanall: cleandisk clean
	@echo "# Cleaning directory tree"
	@find Libraries -name "*.o" | xargs rm -f
	@find Libraries -name "*.a" | xargs rm -f
	@find applications -name "*.o" | xargs rm -f

# haha
clena: clean
clean:
	@find source -name "*.o" | xargs rm -f
	@find source -name "*.cpp.d" | xargs rm -f









