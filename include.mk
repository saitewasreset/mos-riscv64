# ENDIAN is either EL (little endian) or EB (big endian)
# qemu-system-riscv64 -machine virt 只支持小端

QEMU           := qemu-system-riscv64

CROSS_COMPILE  := riscv64-unknown-elf-
CC             := $(CROSS_COMPILE)gcc

CWARNINGS      := -Wall -Wextra -Wpedantic -Wshadow -Wfloat-equal -Wsign-conversion -Wsign-promo -Wunused -Wunused-parameter -Wlogical-op -Wmissing-noreturn -Wnested-externs -Wpointer-arith

CFLAGS         += $(CWARNINGS) --std=gnu99 -mlittle-endian -march=rv64imafdch -mcmodel=medany \
                    -nostdlib -nostartfiles -ffreestanding -fno-builtin -fno-stack-protector \
                    -fno-omit-frame-pointer
LD             := $(CROSS_COMPILE)ld
LDFLAGS        += -static -nostdlib

HOST_CC        := cc
HOST_CFLAGS    += --std=gnu99 -O2 -Wall
