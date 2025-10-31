#pragma once
#include "vm.h"
#include <stdbool.h>
#include <stdint.h>

static inline uint32_t make_logical(u16 seg_idx, u16 offset) {
    return ((uint32_t)seg_idx << 16) | (uint32_t)offset;
}
static inline u16 lo16(u32 x) { return (u16)(x & 0xFFFFu);}
static inline u16 hi16(u32 x) { return (u16)((x >> 16) & 0xFFFFu);} 
static inline uint16_t be16_pair(uint8_t hi, uint8_t lo){
    return (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
}
static inline uint16_t be16p(const uint8_t* p){
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

bool translate_and_check_data (VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16* out_phys);
bool translate_and_check_instr(VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16* out_phys);
bool translate_and_check(VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16* out_phys);


bool mem_read_u8(VM* vm, u16 seg_idx, u16 offset, u32* out_value);
bool mem_read_u16(VM* vm, u16 seg_idx, u16 offset, u32* out_value);
bool mem_read_u32(VM* vm, u16 seg_idx, u16 offset, u32* out_value);

bool mem_write_u8(VM* vm, u16 seg_idx, u16 offset, u32 value);
bool mem_write_u16(VM* vm, u16 seg_idx, u16 offset, u32 value);
bool mem_write_u32(VM* vm, u16 seg_idx, u16 offset, u32 value);

bool code_read_bytes(VM* vm, u16 phys,void* dst, u16 nbytes);
