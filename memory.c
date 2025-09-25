#include "memory.h"
#include <string.h>
#include <stdio.h>

static inline void set_lar_mar(VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16 phys){
    vm->reg[LAR] = ((u32)seg_idx << 16)| offset;
    vm->reg[MAR] = ((u32)nbytes << 16) | phys;
}
bool translate_and_check(VM* vm, u16 seg_idx, u16 offset, u16 nbytes, u16* out_phys){
    
    if(seg_idx >= SEG_COUNT) return false;

    u16 base = vm->seg[seg_idx].base;
    u16 size = vm->seg[seg_idx].size;

    u32 phys = (u32)base + (u32)offset;
    if (nbytes == 0) return false; 
    u32 last = phys + (u32)nbytes - 1u;

    u32 seg_start = base;
    u32 seg_last = (u32)base + (u32)size - 1u;

    if(phys < seg_start || last > seg_last) return false;
    *out_phys = (u16)phys;
    return true;
}
static bool read_bytes(VM* vm, u16 seg_idx, u16 offset, void* dst, u16 nbytes){
    u16 phys;
    if(!translate_and_check(vm, seg_idx, offset, nbytes, &phys)){
        return false;
    }
    set_lar_mar(vm, seg_idx, offset, nbytes, phys);
    memcpy(dst, &vm->ram[phys], nbytes);

    u32 mbr=0;
    memcpy(&mbr, &vm->ram[phys], nbytes);
    vm->reg[MBR]=mbr;
    return true;
}
static bool write_bytes(VM* vm, u16 seg_idx, u16 offset, const void* src, u16 nbytes){
    u16 phys;
    if(!translate_and_check(vm, seg_idx, offset, nbytes, &phys)){
        return false;
    }
    set_lar_mar(vm, seg_idx, offset, nbytes, phys);
    u32 mbr=0;
    memcpy(&mbr, src, nbytes);
    vm->reg[MBR]=mbr;

    memcpy(&vm->ram[phys], src, nbytes);
    return true;
}
bool mem_read_u8(VM* vm, u16 seg_idx, u16 offset, u32* out_value){
    u8 value=0;
    if(!read_bytes(vm, seg_idx, offset, &value, 1)){
        return false;
    }
    *out_value = value;
    return true;
}
bool mem_read_u16(VM* vm, uint16_t s, uint16_t o, uint32_t* out){
    uint8_t b[2];
    if(!read_bytes(vm,s,o,b,2)) return false;
    *out = ((uint32_t)b[0] << 8) | (uint32_t)b[1]; 
    return true;
}
bool mem_read_u32(VM* vm, uint16_t s, uint16_t o, uint32_t* out){
    uint8_t b[4];
    if(!read_bytes(vm,s,o,b,4)) return false;
    *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    return true;
}

bool mem_write_u8(VM* vm, u16 seg_idx, u16 offset, u32 value){
    u8 v=(u8)value;
    return write_bytes(vm, seg_idx, offset, &v, 1);
}
bool mem_write_u16(VM* vm, uint16_t s, uint16_t o, uint32_t val){
    uint8_t b[2] = { (uint8_t)(val>>8), (uint8_t)(val) };
    return write_bytes(vm,s,o,b,2);
}
bool mem_write_u32(VM* vm, uint16_t s, uint16_t o, uint32_t val){
    uint8_t b[4] = {
      (uint8_t)(val>>24), (uint8_t)(val>>16),
      (uint8_t)(val>>8),  (uint8_t)(val)
    };
    return write_bytes(vm,s,o,b,4);
}

bool code_read_bytes(VM* vm, u16 phys,void* dst, u16 nbytes){
    if(phys + nbytes > vm->code_size){
        return false;
    }
    memcpy(dst, &vm->ram[phys], nbytes);
    return true;
}
