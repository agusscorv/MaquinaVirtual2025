#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define RAM_SIZE 16384 // 16KB
#define SEG_COUNT 8
#define REG_COUNT 32

typedef struct {
    u16 base;
    u16 size;
} SegmentDescriptor;

typedef struct {
    u8 ram[RAM_SIZE];
    SegmentDescriptor seg[SEG_COUNT];
    u32 reg[REG_COUNT];
    bool disassemble;
    u16 code_size;
} VM;

enum{
    LAR=0, MAR=1, MBR=2, IP=3, OPC=4, OP1=5, OP2=6, 
    EAX=10, EBX=11, ECX=12, EDX=13, EEX=14, EFX=15,
    AC=16, CC=17,
    CS=26, DS=27
};

void vm_init(VM* vm, bool disassemble);
bool vm_load(VM* vm, const char* path);
int vm_run(VM* vm);