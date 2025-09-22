#pragma once
#include "vm.h"
#include "decoder.h"

typedef int (*OpHandler)(VM*, const DecodedInst*);

void init_dispatch_table(OpHandler table[256]);
int  exec_instruction(VM* vm, const DecodedInst* di, OpHandler table[256]);

bool read_operand_u32(VM* vm, const DecodedOp* op, uint32_t* out);
bool write_operand_u32(VM* vm, const DecodedOp* op, uint32_t val);

static inline void set_NZ(VM* vm, uint32_t result){
    u32 N = (result & 0x80000000u)?1u:0u;
    u32 Z = (result == 0)?1u:0u;
    vm->reg[CC] = (N << 31) | (Z << 30);
}