#include <lib.h>

int pageref(void *va) { return syscall_pageref(va); }