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

static bool fetch_bytes(VM* vm, uint16_t seg, uint16_t off, uint16_t n, uint16_t* phys, uint8_t* dst){
    if (!translate_and_check(vm, seg, off, n, phys)) return false;
    if (!code_read_bytes(vm, *phys, dst, n)) return false;  
    return true;
}

static inline bool is_two_ops(uint8_t opc){ return (opc >= 0x10 && opc <= 0x1F); } 
static inline bool is_one_op (uint8_t opc){
    return (opc <= 0x08) || opc == 0x0B || opc == 0x0C || opc == 0x0D;
}
static inline bool is_zero_op(uint8_t opc){
    return (opc == 0x0E) || (opc == 0x0F);
}
      
static inline uint32_t pack_op(OperandType t, const uint8_t* raw, uint8_t n){
    uint32_t v = ((uint32_t)(t & 0x3)) << 24;
    if (n > 3) n = 3;
    for (uint8_t i = 0; i < n; ++i){
        v |= ((uint32_t)raw[i]) << (8 * (2 - i));
    }
    return v;
}

bool fetch_and_decode(VM* vm, DecodedInst* di){
    /* Tomar IP actual */
    u16 seg = (u16)(vm->reg[IP] >> 16);
    u16 off = (u16)(vm->reg[IP] & 0xFFFFu);

    /* Lee primer byte de instrucción */
    u16 phys0 = 0;
    u8  hdr   = 0;
    if (!fetch_bytes(vm, seg, off, 1, &phys0, &hdr)) {
        return false;
    }

    /* Inicializar la estructura de instrucción decodificada */
    memset(di, 0, sizeof(*di));
    di->phys   = phys0;          
    di->size   = 1;
    di->opcode = (u8)(hdr & 0x1F);  
    di->A.type = OT_NONE;
    di->B.type = OT_NONE;

    vm->reg[OPC] = (u32)di->opcode;
    vm->reg[OP1] = 0;
    vm->reg[OP2] = 0;

    /*Determinar cuántos operandos tiene y sus tipos */
    if (is_two_ops(di->opcode)){
        OperandType typeB = type_from_code((u8)(hdr >> 6));
        OperandType typeA = ((hdr >> 5) & 0x1) ? OT_MEM : OT_REG;

        u8 nB = size_from_type(typeB);
        u8 nA = size_from_type(typeA);

        if (nB){
            u16 physB = 0;
            if (!fetch_bytes(vm, seg, (u16)(off + di->size), nB, &physB, di->B.raw))
                return false;
            di->B.type = typeB;
            di->B.size = nB;
            di->size  += nB;
        }

        if (nA){
            u16 physA = 0;
            if (!fetch_bytes(vm, seg, (u16)(off + di->size), nA, &physA, di->A.raw))
                return false;
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }

    } else if (is_one_op(di->opcode)){

        OperandType typeA = type_from_code((u8)(hdr >> 6));
        u8 nA = size_from_type(typeA);

        if (nA){
            u16 physA = 0;
            if (!fetch_bytes(vm, seg, (u16)(off + di->size), nA, &physA, di->A.raw))
                return false;
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }

    } else if (is_zero_op(di->opcode)){
        /* STOP, RET, etc. sin operandos */
    } else {
        /* opcode inválido */
        return false;
    }
    {
        u32 descA = 0;
        u32 descB = 0;

        if (di->B.type == OT_REG) {
            u8 reg_code = (u8)(di->B.raw[0] & 0x1F);
            descB = (0x01u << 24) | (u32)reg_code;
        } else if (di->B.type == OT_IMM) {
            u16 imm = be16_pair(di->B.raw[0], di->B.raw[1]);
            descB = (0x02u << 24) | (u32)imm;
        } else if (di->B.type == OT_MEM) {
            u8 base = (u8)(di->B.raw[0] & 0x1F);
            int16_t disp = (int16_t)((di->B.raw[1] << 8) | di->B.raw[2]);
            descB = (0x03u << 24)
                  | ((u32)base << 16)
                  | ((u16)disp & 0xFFFFu);
        }

        if (di->A.type == OT_REG) {
            u8 reg_code = (u8)(di->A.raw[0] & 0x1F);
            descA = (0x01u << 24) | (u32)reg_code;
        } else if (di->A.type == OT_IMM) {
            u16 imm = be16_pair(di->A.raw[0], di->A.raw[1]);
            descA = (0x02u << 24) | (u32)imm;
        } else if (di->A.type == OT_MEM) {
            u8 base = (u8)(di->A.raw[0] & 0x1F);
            int16_t disp = (int16_t)((di->A.raw[1] << 8) | di->A.raw[2]);
            descA = (0x03u << 24)
                  | ((u32)base << 16)
                  | ((u16)disp & 0xFFFFu);
        }

        vm->reg[OP1] = descA;
        vm->reg[OP2] = descB;
    }

    /*  Avanzar IP  */
    {
        u16 new_off = (u16)(off + di->size);
        vm->reg[IP] = ((u32)seg << 16) | (u32)new_off;
    }

    return true;
}
