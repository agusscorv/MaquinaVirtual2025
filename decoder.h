#pragma once
#include "vm.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum{
    OT_NONE = 0,
    OT_REG = 1,
    OT_IMM = 2,
    OT_MEM = 3
} OperandType;

typedef struct{
    u8 type;
    u8 raw[3];
    u8 size; 
} DecodedOp;

typedef struct{
    u8 opcode_low;
    u8 opcode;
    DecodedOp A;
    DecodedOp B;
    u16 size;
    u16 phys;
} DecodedInst;
static inline uint16_t be16(uint8_t hi, uint8_t lo){
  return (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
}


static inline int16_t be16s(uint8_t hi, uint8_t lo){
  return (int16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
}

static inline uint8_t size_from_type(OperandType t){
  switch (t){
    case OT_NONE: return 0;
    case OT_REG:  return 1;
    case OT_IMM:  return 2;
    case OT_MEM:  return 3;
    default:      return 0;
  }
}

static inline uint8_t type_size(OperandType t){ return size_from_type(t); }

bool fetch_and_decode(VM* vm, DecodedInst* inst);