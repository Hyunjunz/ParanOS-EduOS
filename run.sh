clear
make clean
make
qemu-system-i386 -cdrom build/os.iso -serial stdio -m 256M 