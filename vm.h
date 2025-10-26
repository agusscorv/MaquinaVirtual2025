#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define RAM_DEFAULT_KIB 16     
#define REG_COUNT 32
#define SEG_COUNT 8

typedef struct {
    u16 base;   
    u16 size;   
} SegmentDescriptor;

enum {
    SEG_PARAM = 0,   
    SEG_CONST = 1,   
    SEG_CODE  = 2,
    SEG_DATA  = 3,
    SEG_EXTRA = 4,
    SEG_STACK = 5,

};
enum {
    LAR = 0, MAR = 1, MBR = 2, IP  = 3,
    OPC = 4, OP1 = 5, OP2 = 6,
    SP  = 7,  BP  = 8,
    R9_RES = 9,
    EAX = 10, EBX = 11, ECX = 12, EDX = 13, EEX = 14, EFX = 15,
    AC  = 16, CC  = 17,
    R18_RES = 18, R19_RES = 19, R20_RES = 20, R21_RES = 21,
    R22_RES = 22, R23_RES = 23, R24_RES = 24, R25_RES = 25,
    CS = 26, DS = 27, ES = 28, SS = 29, KS = 30, PS = 31
};
typedef struct {
    u8  ram[RAM_DEFAULT_KIB * 1024]; 
    SegmentDescriptor seg[SEG_COUNT];
    u32 reg[REG_COUNT];

    bool disassemble;         
    u32  ram_kib;             

    const char* opt_vmx_path;
    const char* opt_vmi_path;
    int  have_vmx;
    int  have_vmi;

    int  idx_param;
    int  idx_const;
    int  idx_code;
    int  idx_data;
    int  idx_extra;
    int  idx_stack;


    int  have_params;
    int  argc_on_stack;

    u16  code_size;           
} VM;

void vm_init(VM* vm, bool disassemble);

bool vm_load(VM* vm, char** params, int argc);

int  vm_run(VM* vm);

bool vm_save_vmi(VM* vm, const char* path);

bool vm_load_vmi(VM* vm, const char* path);
