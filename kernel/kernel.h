/* kernel.h - global kernel information */
#ifndef KERNEL_KERNEL_H
#define KERNEL_KERNEL_H

#define MYOS_NAME     "myOS"
#define MYOS_VERSION  "1.0"

/* Name of the bootloader that started us ("unknown" if not reported) */
const char* kernel_bootloader(void);

#endif
