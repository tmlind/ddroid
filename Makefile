TIMESTAMP=$$(date +%Y-%m-%d)

ifeq ($(wildcard system/etc/kexec/uart.ko),)
$(error Missing droid4-mainline-kexec-0.3 system/etc/kexec/uart.ko)
endif

ifeq ($(wildcard system/etc/kexec/arm_kexec.ko),)
$(error Missing droid4-mainline-kexec-0.3 system/etc/kexec/arm_kexec.ko)
endif

ifeq ($(wildcard system/etc/kexec/kexec.ko),)
$(error Missing droid4-mainline-kexec-0.3 system/etc/kexec/kexec.ko)
endif

ifeq ($(wildcard system/etc/kexec/kexec.static),)
$(error Missing droid4-mainline-kexec-0.3 kexec to system/etc/kexec/kexec.static)
endif

ifeq ($(wildcard system/etc/kexec/kernel),)
$(error Place mainline kernel zImage to system/etc/kexec/kernel)
endif

ifeq ($(wildcard system/etc/kexec/devtree),)
$(error Place mainline omap4-droid4-xt894.dtb to system/etc/kexec/devtree)
endif

ifeq ($(wildcard system/etc/kexec/ramdisk.img),)
$(warning No initramfs found at system/etc/kexec/ramdisk.img)
endif

zip:
	rm -f ../ddroid-$(TIMESTAMP).zip
	zip -qr ../ddroid-$(TIMESTAMP).zip META-INF install system .safestrapped
	echo "Zipped up ../ddroid-$(TIMESTAMP).zip, now run make push"

push: zip
	adb push ../ddroid-$(TIMESTAMP).zip /sdcard/
