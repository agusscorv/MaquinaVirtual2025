#pragma once
#include "vm.h"
#include "decoder.h"
#include <stddef.h>

const char* opcode_mnemonic(u8 opcode);
const char* reg_name(u8 idx);

void format_operand(const DecodedOp* op, char* out,size_t cap);

void disasm_print(VM* vm, const DecodedInst* di);
