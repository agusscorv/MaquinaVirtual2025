#include "memory.h"
#include <string.h>
#include <stdio.h>


static inline void set_lar_mar(VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16 phys){
    vm->reg[LAR] = ((u32)seg_idx << 16) | (u32)offset;
    vm->reg[MAR] = ((u32)nbytes  << 16) | (u32)phys;
}


bool translate_and_check(VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16* out_phys){
    if (seg_idx >= SEG_COUNT) return false;
    if (nbytes == 0)          return false;

    u16 base = vm->seg[seg_idx].base;
    u16 size = vm->seg[seg_idx].size;

    if (size == 0) return false;

    u32 phys = (u32)base + (u32)offset;
    u32 last = phys + (u32)nbytes - 1u;

    u32 seg_start = (u32)base;
    u32 seg_last  = (u32)base + (u32)size - 1u;

    if (phys < seg_start || last > seg_last) return false;

    if (out_phys) *out_phys = (u16)phys;
    return true;
}


static bool read_bytes(VM* vm, u16 seg_idx, u16 offset, void* dst, u16 nbytes){
    u16 phys;
    if (!translate_and_check(vm, seg_idx, offset, nbytes, &phys)){
        return false;
    }
    set_lar_mar(vm, seg_idx, offset, nbytes, phys);

    memcpy(dst, &vm->ram[phys], nbytes);

    u32 mbr = 0;
    const u8* p = &vm->ram[phys];
    switch (nbytes){
        case 1: mbr = (u32)p[0]; break;
        case 2: mbr = ((u32)p[0] << 8) |  (u32)p[1]; break;
        case 4: mbr = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; break;
        default: return false;
    }
    vm->reg[MBR] = mbr;
    return true;
}

static bool write_bytes(VM* vm, u16 seg_idx, u16 offset, const void* src, u16 nbytes){
    u16 phys;
    if (!translate_and_check(vm, seg_idx, offset, nbytes, &phys)){
        return false;
    }
    set_lar_mar(vm, seg_idx, offset, nbytes, phys);

    const u8* p = (const u8*)src;
    u32 mbr = 0;
    switch (nbytes){
        case 1: mbr = (u32)p[0]; break;
        case 2: mbr = ((u32)p[0] << 8) |  (u32)p[1]; break;
        case 4: mbr = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; break;
        default: return false;
    }
    vm->reg[MBR] = mbr;

    memcpy(&vm->ram[phys], src, nbytes);
    return true;
}


bool mem_read_u8 (VM* vm, u16 seg_idx, u16 offset, u32* out_value){
    u8 value = 0;
    if (!read_bytes(vm, seg_idx, offset, &value, 1)) return false;
    *out_value = (u32)value;
    return true;
}

bool mem_read_u16(VM* vm, u16 seg_idx, u16 offset, u32* out_value){
    u8 b[2];
    if (!read_bytes(vm, seg_idx, offset, b, 2)) return false;
    *out_value = ((u32)b[0] << 8) | (u32)b[1];
    return true;
}

bool mem_read_u32(VM* vm, u16 seg_idx, u16 offset, u32* out_value){
    u8 b[4];
    if (!read_bytes(vm, seg_idx, offset, b, 4)) return false;
    *out_value = ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | (u32)b[3];
    return true;
}

bool mem_write_u8 (VM* vm, u16 seg_idx, u16 offset, u32 value){
    u8 b = (u8)value;
    return write_bytes(vm, seg_idx, offset, &b, 1);
}

bool mem_write_u16(VM* vm, u16 seg_idx, u16 offset, u32 value){
    u8 b[2] = { (u8)(value >> 8), (u8)(value) };
    return write_bytes(vm, seg_idx, offset, b, 2);
}

bool mem_write_u32(VM* vm, u16 seg_idx, u16 offset, u32 value){
    u8 b[4] = {
        (u8)(value >> 24), (u8)(value >> 16),
        (u8)(value >> 8),  (u8)(value)
    };
    return write_bytes(vm, seg_idx, offset, b, 4);
}


bool code_read_bytes(VM* vm, u16 phys, void* dst, u16 nbytes){
    const u16 code_base = vm->seg[SEG_CODE].base;
    const u16 code_size = vm->seg[SEG_CODE].size;

    if (code_size == 0) return false;                
    if (nbytes == 0)  return false;

    const u32 start = (u32)phys;
    const u32 end   = start + (u32)nbytes - 1u;
    const u32 lo    = (u32)code_base;
    const u32 hi    = (u32)code_base + (u32)code_size - 1u;

    if (start < lo || end > hi) return false;

    memcpy(dst, &vm->ram[phys], nbytes);
    return true;
}
