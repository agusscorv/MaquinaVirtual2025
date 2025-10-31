#include "decoder.h"
#include "memory.h"
#include "vm.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static inline OperandType type_from_code(uint8_t code){
    switch (code & 0x3){
        case 0: return OT_NONE;  // 00
        case 1: return OT_REG;   // 01
        case 2: return OT_IMM;   // 10
        case 3: return OT_MEM;   // 11
    }
    return OT_NONE;
}

static inline bool is_two_ops(uint8_t opc){
    return (opc >= 0x10 && opc <= 0x1F);
}
static inline bool is_one_op(uint8_t opc){
    return (opc <= 0x08) || opc == 0x0B || opc == 0x0C || opc == 0x0D;
}
static inline bool is_zero_op(uint8_t opc){
    return (opc == 0x0E) || (opc == 0x0F);
}

static inline uint32_t desc_from_operand(const DecodedOp* op){
    uint32_t d = 0;

    switch (op->type){
    case OT_REG: {
        uint8_t reg_code = (uint8_t)(op->raw[0] & 0x1F);
        d = (0x01u << 24) | (uint32_t)reg_code;
    } break;

    case OT_IMM: {
        uint16_t imm = be16_pair(op->raw[0], op->raw[1]);
        d = (0x02u << 24) | (uint32_t)imm;
    } break;

    case OT_MEM: {
        uint8_t  base = (uint8_t)(op->raw[0] & 0x1F);
        int16_t  disp = (int16_t)((op->raw[1] << 8) | op->raw[2]);
        d = (0x03u << 24)
          | ((uint32_t)base << 16)
          | ((uint16_t)disp & 0xFFFFu);
    } break;

    case OT_NONE:
    default:
        d = 0;
        break;
    }
    return d;
}

static bool fetch_bytes_instr(VM* vm, uint16_t seg, uint16_t off, uint16_t n, uint16_t* phys, uint8_t* dst)
{
    if (!translate_and_check_instr(vm, seg, off, n, phys)){
        return false;
    }
    if (!code_read_bytes(vm, *phys, dst, n)){
        return false;
    }
    return true;
}


bool fetch_and_decode(VM* vm, DecodedInst* di){
    uint16_t seg = (uint16_t)(vm->reg[IP] >> 16);
    uint16_t off = (uint16_t)(vm->reg[IP] & 0xFFFFu);

    uint16_t phys0 = 0;
    uint8_t  hdr   = 0;
    if (!fetch_bytes_instr(vm, seg, off, 1, &phys0, &hdr)) {
        return false;
    }

    memset(di, 0, sizeof(*di));
    di->phys   = phys0;
    di->size   = 1;

    di->opcode = (uint8_t)(hdr & 0x1F);

    di->A.type = OT_NONE;
    di->B.type = OT_NONE;
    di->A.size = 0;
    di->B.size = 0;


    vm->reg[OPC] = (uint32_t)di->opcode;
    vm->reg[OP1] = 0;
    vm->reg[OP2] = 0;


    if (is_two_ops(di->opcode)){
        OperandType typeB = type_from_code((uint8_t)(hdr >> 6));
        OperandType typeA = ((hdr >> 5) & 0x1) ? OT_MEM : OT_REG;

        uint8_t nB = size_from_type(typeB);
        uint8_t nA = size_from_type(typeA);

        if (nB){
            uint16_t physB = 0;
            if (!fetch_bytes_instr(vm,seg,(uint16_t)(off + di->size),nB,&physB,di->B.raw)){
                return false;
            }
            di->B.type = typeB;
            di->B.size = nB;
            di->size  += nB;
        }

        if (nA){
            uint16_t physA = 0;
            if (!fetch_bytes_instr(vm,seg,(uint16_t)(off + di->size),nA,&physA,di->A.raw)){
                return false;
            }
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }

    } else if (is_one_op(di->opcode)){
        OperandType typeA = type_from_code((uint8_t)(hdr >> 6));
        uint8_t nA = size_from_type(typeA);
        if (nA){
            uint16_t physA = 0;
            if (!fetch_bytes_instr(vm,seg,(uint16_t)(off + di->size),nA,&physA,di->A.raw)){
                return false;
            }
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }


    } else if (is_zero_op(di->opcode)){
        di->A.type = OT_NONE;
        di->B.type = OT_NONE;

    } else {
        return false;
    }

    {
        uint32_t descA = desc_from_operand(&di->A);
        uint32_t descB = desc_from_operand(&di->B);

        vm->reg[OP1] = descA;
        vm->reg[OP2] = descB;
    }

    {
        uint16_t new_off = (uint16_t)(off + di->size);
        vm->reg[IP] = ((uint32_t)seg << 16) | (uint32_t)new_off;
    }

    return true;
}
