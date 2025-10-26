#pragma once
#include "vm.h"
#include "decoder.h"
#include <stddef.h>

const char* opcode_mnemonic(u8 opcode);
const char* reg_name(u8 idx);

void disasm_dump_segments(VM* vm);
void disasm_dump_const_strings(VM* vm);
void disasm_print(VM* vm, const DecodedInst* di);

void disasm_print(VM* vm, const DecodedInst* di);
