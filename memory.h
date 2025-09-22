#pragma once
#include "vm.h"
#include <stdbool.h>
#include <stdint.h>

bool translate_and_check(VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16* out_phys);

static inline uint32_t make_logical(u16 seg_idx, u16 offset) {
    return ((uint32_t)seg_idx << 16) | (uint32_t)offset;
}
static inline u16 lo16(u32 x) { return (u16)(x & 0xFFFFu);}
static inline u16 hi16(u32 x) { return (u16)((x >> 16) & 0xFFFFu);} 

bool mem_read_u8(VM* vm, u16 seg_idx, u16 offset, u32* out_value);
bool mem_read_u16(VM* vm, u16 seg_idx, u16 offset, u32* out_value);
bool mem_read_u32(VM* vm, u16 seg_idx, u16 offset, u32* out_value);

bool mem_write_u8(VM* vm, u16 seg_idx, u16 offset, u32 value);
bool mem_write_u16(VM* vm, u16 seg_idx, u16 offset, u32 value);
bool mem_write_u32(VM* vm, u16 seg_idx, u16 offset, u32 value);

bool code_read_bytes(VM* vm, u16 phys,void* dst, u16 nbytes);
