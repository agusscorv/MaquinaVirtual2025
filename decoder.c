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
    code_read_bytes(vm, *phys, dst, n);  // acceso a "código"
    return true;
}


static inline bool is_two_ops(uint8_t opc){ return (opc >= 0x10 && opc <= 0x1F); } 
static inline bool is_one_op (uint8_t opc){ return (opc <= 0x08); }                
static inline bool is_zero_op(uint8_t opc){ return (opc == 0x0F); }              


static inline uint32_t pack_op(OperandType t, const uint8_t* raw, uint8_t n){
    uint32_t v = ((uint32_t)(t & 0x3)) << 24;
    if (n > 3) n = 3;
    for (uint8_t i = 0; i < n; ++i){
        v |= ((uint32_t)raw[i]) << (8 * (2 - i));
    }
    return v;
}


bool fetch_and_decode(VM* vm, DecodedInst* di){
    uint16_t seg = (uint16_t)(vm->reg[IP] >> 16);
    uint16_t off = (uint16_t)(vm->reg[IP] & 0xFFFFu);

    uint16_t phys0 = 0;
    uint8_t  hdr   = 0;
    if (!fetch_bytes(vm, seg, off, 1, &phys0, &hdr)) return false;

    memset(di, 0, sizeof(*di));
    di->phys   = phys0;
    di->size   = 1;
    di->opcode = (uint8_t)(hdr & 0x1F);      
    di->A.type = OT_NONE;
    di->B.type = OT_NONE;

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
            if (!fetch_bytes(vm, seg, (uint16_t)(off + di->size), nB, &physB, di->B.raw)) return false;
            di->B.type = typeB;
            di->B.size = nB;
            di->size  += nB;
        }

        if (nA){
            uint16_t physA = 0;
            if (!fetch_bytes(vm, seg, (uint16_t)(off + di->size), nA, &physA, di->A.raw)) return false;
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }

    } else if (is_one_op(di->opcode)){
        OperandType typeA = type_from_code((uint8_t)(hdr >> 6));
        uint8_t nA   = size_from_type(typeA);

        if (nA){
            uint16_t physA = 0;
            if (!fetch_bytes(vm, seg, (uint16_t)(off + di->size), nA, &physA, di->A.raw)) return false;
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }
    } else if (is_zero_op(di->opcode)){
    } else {
        return false;
    }


    vm->reg[OP1] = pack_op(di->A.type, di->A.raw, di->A.size); 
    vm->reg[OP2] = pack_op(di->B.type, di->B.raw, di->B.size);

    uint16_t new_off = (uint16_t)(off + di->size);
    vm->reg[IP] = ((uint32_t)seg << 16) | new_off;              

        uint32_t descA = 0, descB = 0;

    // --- Operando B ---
    if (di->B.type == OT_REG) {
        uint8_t reg_code = di->B.raw[0] & 0x1F;
        descB = (0x01u << 24) | (uint32_t)reg_code;
    }
    else if (di->B.type == OT_IMM) {
        uint16_t imm = be16(di->B.raw[0], di->B.raw[1]);
        descB = (0x02u << 24) | (uint32_t)imm;
    }
    else if (di->B.type == OT_MEM) {
        uint8_t base = di->B.raw[0] & 0x1F;
        int16_t disp = (int16_t)((di->B.raw[1] << 8) | di->B.raw[2]);
        descB = (0x03u << 24) | ((uint32_t)base << 16) | ((uint16_t)disp & 0xFFFFu);
    }

    // --- Operando A ---
    if (di->A.type == OT_REG) {
        uint8_t reg_code = di->A.raw[0] & 0x1F;
        descA = (0x01u << 24) | (uint32_t)reg_code;
    }
    else if (di->A.type == OT_IMM) {
        uint16_t imm = be16(di->A.raw[0], di->A.raw[1]);
        descA = (0x02u << 24) | (uint32_t)imm;
    }
    else if (di->A.type == OT_MEM) {
        uint8_t base = di->A.raw[0] & 0x1F;
        int16_t disp = (int16_t)((di->A.raw[1] << 8) | di->A.raw[2]);
        descA = (0x03u << 24) | ((uint32_t)base << 16) | ((uint16_t)disp & 0xFFFFu);
    }

    vm->reg[OP1] = descA;
    vm->reg[OP2] = descB;

    // Avanzar IP al final de la instrucción
    vm->reg[IP] = ((uint32_t)seg << 16) | (uint32_t)(off + di->size);

    return true;
}

    
