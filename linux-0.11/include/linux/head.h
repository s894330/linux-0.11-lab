#ifndef _HEAD_H
#define _HEAD_H

/* 8byte */
typedef struct descriptor_struct {
	unsigned long a,b;
} descriptor_table[256];

extern unsigned long pg_dir[1024];
extern descriptor_table idt, gdt;

#define GDT_NUL 0
#define GDT_CODE 1
#define GDT_DATA 2
#define GDT_TMP 3

#define KERNEL_CODE_SEG 0x08
#define KERNEL_DATA_SEG 0x10

#define LDT_NUL 0
#define LDT_CODE 1
#define LDT_DATA 2

#define TASK_CODE_SEG 0x0f
#define TASK_DATA_SEG 0x17

#endif
