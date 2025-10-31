#include "cpu.h"
#include "decoder.h"
#include "disasm.h"
#include "memory.h"
#include "vm.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <ctype.h>

static inline uint16_t hi16_u32(uint32_t x){ return (uint16_t)(x >> 16); }
static inline uint16_t lo16_u32(uint32_t x){ return (uint16_t)(x & 0xFFFFu); }
static inline uint32_t shamt32(uint32_t v){ return v & 31u; }
static inline uint32_t cc_Nbit(VM* vm){ return (vm->reg[CC] >> 31) & 1u; }
static inline uint32_t cc_Zbit(VM* vm){ return (vm->reg[CC] >> 30) & 1u; }
static inline uint16_t ecx_size(uint32_t ecx) {return (uint16_t)(ecx >> 16);}
static inline uint16_t ecx_count(uint32_t ecx) {
    uint16_t ch = (uint16_t)((ecx >> 8) & 0xFFu);
    if (ch != 0) {
        return ch;
    }
    return (uint16_t)(ecx & 0xFFFFu);
}
enum { MODE_DEC=0x01, MODE_CHR=0x02, MODE_OCT=0x04, MODE_HEX=0x08, MODE_BIN=0x10 };
static inline void jump_to_code(VM* vm, uint16_t off){
  uint16_t code_seg = hi16_u32(vm->reg[CS]);               
  vm->reg[IP] = ((uint32_t)code_seg << 16) | (uint32_t)off; 
}
static inline uint8_t mem_size_code(const DecodedOp* op){ return (uint8_t)(op->raw[0] >> 6); }
static inline uint16_t mem_size_from_code(uint8_t c){
    switch (c & 0x3){ case 0: return 4; case 2: return 2; case 3: return 1; default: return 4; }
}
enum { REG_SECT_32=0, REG_SECT_16=1, REG_SECT_8H=2, REG_SECT_8L=3 };
static inline uint8_t reg_sector(const DecodedOp* op){ return (uint8_t)((op->raw[0] >> 5) & 0x3); }
static inline uint32_t sext_from8(uint32_t v){ return (uint32_t)(int32_t)(int8_t)(v & 0xFFu); }
static inline uint32_t sext_from16(uint32_t v){ return (uint32_t)(int32_t)(int16_t)(v & 0xFFFFu); }

static inline void term_clear(void){
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
}

static int single_step(VM* vm){
    if (vm->reg[IP] == 0xFFFFFFFFu) return 0;

    uint16_t seg = (uint16_t)(vm->reg[IP] >> 16);
    uint16_t off = (uint16_t)(vm->reg[IP] & 0xFFFFu);

    if (off == vm->seg[seg].size) return 0;

    if (off > vm->seg[seg].size){
        fprintf(stderr,"Error: fallo de segmento\n");
        return -1;
    }

    uint16_t phys;
    if (!translate_and_check_instr(vm, seg, off, 1, &phys)){
        fprintf(stderr,"Error: instruccion invalida\n");
        return -1;
    }

    DecodedInst di;
    if (!fetch_and_decode(vm, &di)){
        uint32_t opc=0xFF;
        (void)mem_read_u8(vm, seg, off, &opc);
        fprintf(stderr,"Error: instruccion invalida OPC=%02X\n", (unsigned)opc);
        return -1;
    }

    if (vm->disassemble){
        disasm_print(vm, &di);
    }

    OpHandler tb[256];
    init_dispatch_table(tb);
    int rc = exec_instruction(vm, &di, tb);
    return (rc<0)? -1 : 0;
}


static inline uint16_t ss_index(VM* vm){
    return (vm->reg[SS] == 0xFFFFFFFFu) ? 0xFFFFu : hi16_u32(vm->reg[SS]);
}
static inline uint16_t sp_off(VM* vm){
    return (uint16_t)(vm->reg[SP] & 0xFFFFu);
}
static inline void set_sp_off(VM* vm, uint16_t off){
    uint32_t seg = (vm->reg[SS] & 0xFFFF0000u);
    vm->reg[SP] = seg | (uint32_t)off;
}
static int stack_push32(VM* vm, uint32_t val){
    if (vm->reg[SS] == 0xFFFFFFFFu || vm->reg[SP] == 0xFFFFFFFFu){
        fprintf(stderr, "Error: stack overflow (SS/SP inválidos)\n");
        return -1;
    }
    uint16_t seg = ss_index(vm);
    uint16_t sp  = sp_off(vm);
    if (sp < 4){ fprintf(stderr,"Error: stack overflow\n"); return -1; }
    sp -= 4;
    if (!mem_write_u32(vm, seg, sp, val)) { fprintf(stderr,"Error: stack overflow\n"); return -1; }
    set_sp_off(vm, sp);
    return 0;
}
static int stack_pop32(VM* vm, uint32_t* out){
    uint16_t seg = ss_index(vm);
    uint16_t sp  = sp_off(vm);

    if (vm->seg[seg].size == 0 || sp + 4 > vm->seg[seg].size) {
        fprintf(stderr, "Error: stack underflow (pila vacia o bytes insuficientes)\n"); 
        return -1; 
    }

    uint32_t v = 0;
    if (!mem_read_u32(vm, seg, sp, &v)) { 
         fprintf(stderr,"Error: stack underflow\n");
         return -1; 
    }

    sp += 4;
    set_sp_off(vm, sp);
    *out = v;
    return 0;
}
static inline uint8_t vm_regidx_from_vmxcode(uint8_t code){
    switch (code){
        case 0x00: return LAR;
        case 0x01: return MAR;
        case 0x02: return MBR;
        case 0x03: return IP;
        case 0x04: return OPC;
        case 0x05: return OP1;
        case 0x06: return OP2;

        case 0x07: return SP;  
        case 0x08: return BP;  

        case 0x0A: return EAX;
        case 0x0B: return EBX;
        case 0x0C: return ECX;
        case 0x0D: return EDX;
        case 0x0E: return EEX;
        case 0x0F: return EFX;

        case 0x10: return AC;
        case 0x11: return CC;

        case 0x1A: return CS;
        case 0x1B: return DS;
        case 0x1C: return ES;   
        case 0x1D: return SS;   
        case 0x1E: return KS;   
        case 0x1F: return PS;  

        default:   return REG_COUNT;
    }
}

static inline bool is_ds_implicit(uint8_t b){ return b==0x0F || b==0xF0; } 
static bool get_mem_address(VM* vm, const DecodedOp* op, u16* seg, u16* off){
    if(op->type != OT_MEM) return false;
    uint8_t base_byte = op->raw[0];
    int16_t disp = (int16_t)((op->raw[1]<<8) | op->raw[2]);  
    uint8_t r = is_ds_implicit(base_byte) ? DS: vm_regidx_from_vmxcode(base_byte & 0x1F);
    if (r == REG_COUNT) return false;
    u32 ptr = vm->reg[r];                
    *seg = (u16)(ptr >> 16);
    *off = (u16)((ptr & 0xFFFFu) + (u16)disp);
    return true;
}
bool read_operand_u32(VM* vm, const DecodedOp* op, uint32_t* out){
    switch(op->type){
        case OT_NONE:
            *out = 0; return true;

        case OT_REG: {
            uint8_t r = vm_regidx_from_vmxcode(op->raw[0] & 0x1F);
            if (r == REG_COUNT) return false;
            uint32_t full = vm->reg[r];
            switch (reg_sector(op)){
                case REG_SECT_32: *out = full;                 return true;
                case REG_SECT_16: *out = sext_from16(full);    return true;  // AX
                case REG_SECT_8H:  *out = sext_from8(full>>8); return true;  // AH
                case REG_SECT_8L:  *out = sext_from8(full);    return true;  // AL
                default: return false;
            }
        }
        case OT_IMM: {
            /* MV1/MV2 inmediato = 16 bits, con signo */
            int16_t s16 = (int16_t)be16_pair(op->raw[0], op->raw[1]);
            *out = (uint32_t)(int32_t)s16;
            return true;
        }
        case OT_MEM: {
            u16 seg, off;
            if(!get_mem_address(vm, op, &seg, &off)) return false;
            uint16_t sz = mem_size_from_code(mem_size_code(op));
            switch (sz){
                case 1: { uint32_t v=0; if(!mem_read_u8 (vm, seg, off, &v)) return false; *out = sext_from8(v);  return true; }
                case 2: { uint32_t v=0; if(!mem_read_u16(vm, seg, off, &v)) return false; *out = sext_from16(v); return true; }
                case 4: { uint32_t v=0; if(!mem_read_u32(vm, seg, off, &v)) return false; *out = v;             return true; }
                default: return false;
            }
        }
    }
    return false;
}
bool write_operand_u32(VM* vm, const DecodedOp* op, uint32_t val){
    switch(op->type){
        case OT_REG: {
            uint8_t r = vm_regidx_from_vmxcode(op->raw[0] & 0x1F);
            if (r == REG_COUNT) return false;
            uint32_t old = vm->reg[r];
            switch (reg_sector(op)){
                case REG_SECT_32: vm->reg[r] = val;                         return true;  // EAX
                case REG_SECT_16: vm->reg[r] = (old & 0xFFFF0000u) | (val & 0xFFFFu); return true; // AX
                case REG_SECT_8H: vm->reg[r] = (old & 0xFFFF00FFu) | ((val & 0xFFu) << 8); return true; // AH
                case REG_SECT_8L: vm->reg[r] = (old & 0xFFFFFF00u) | (val & 0xFFu); return true; // AL
                default: return false;
            }
        }
        case OT_MEM: {
            u16 seg, off;
            if(!get_mem_address(vm, op, &seg, &off)) return false;
            uint16_t sz = mem_size_from_code(mem_size_code(op));
            switch (sz){
                case 1: return mem_write_u8 (vm, seg, off, val);
                case 2: return mem_write_u16(vm, seg, off, val);
                case 4: return mem_write_u32(vm, seg, off, val);
                default: return false;
            }
        }
        case OT_NONE:
        case OT_IMM:
        default:
            return false;
    }
}
static bool phys_of_cell(VM* vm, u32 base_ptr, u16 cell_size, u16 idx, u16* out_phys){
    u16 seg = hi16_u32(base_ptr);
    u16 off = (u16)(lo16_u32(base_ptr) + (idx * cell_size));
    return translate_and_check_data(vm, seg, off, cell_size, out_phys);
}

static bool mem_read_cell(VM* vm, u16 seg, u16 offset, u16 n, u32* out){
     switch(n){
        case 1: return mem_read_u8(vm, seg, offset, out);
        case 2: return mem_read_u16(vm, seg, offset, out);
        case 4: return mem_read_u32(vm, seg, offset, out);
        default: return false;
    }
}
static bool mem_write_cell(VM* vm, u16 seg, u16 offset, u16 n, u32 val){
     switch(n){
        case 1: return mem_write_u8(vm, seg, offset, val);
        case 2: return mem_write_u16(vm, seg, offset, val);
        case 4: return mem_write_u32(vm, seg, offset, val);
        default: return false;
    }
}
static int op_stop(VM* vm, const DecodedInst* di){
    (void)di;
    vm->reg[IP]=0xFFFFFFFFu; 
  return 0;
}
static int op_invalid(VM* vm, const DecodedInst* di){
    (void)vm; (void)di;
    fprintf(stderr, "Error: instrucción inválida OPC=%02X\n", di->opcode);
    return -1;
}
static int op_mov(VM* vm, const DecodedInst* di){
    uint32_t src;
    if(!read_operand_u32(vm, &di->B, &src)) return -1;
    if(!write_operand_u32(vm, &di->A, src)) return -1;
    return 0;
}
static int op_ldl(VM* vm, const DecodedInst* di){
    u32 src, dst;
    if(!read_operand_u32(vm, &di->B, &src)) return -1;
    if(!read_operand_u32(vm, &di->A, &dst)) return -1;
    u32 low16 = (u16)(src & 0xFFFFu);
    u32 res = (dst & 0xFFFF0000u) | low16;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    return 0;
}
static int op_ldh(VM* vm, const DecodedInst* di){
    u32 src, dst;
    if(!read_operand_u32(vm, &di->B, &src)) return -1;
    if(!read_operand_u32(vm, &di->A, &dst)) return -1;
    u32 low16 = (u16)(src & 0xFFFFu);
    u32 res = (dst & 0x0000FFFFu) | (low16 << 16);
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    return 0;
}    
static int op_add(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 res = a + b;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    set_NZ(vm, res);
    return 0;
}
static int op_sub(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 res = a - b;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    set_NZ(vm, res);
    return 0;
}
static int op_mul(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    
    uint64_t prod =(uint64_t)a * (uint64_t)b;
    u32 res = (u32)prod;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    set_NZ(vm, res);
    return 0;
}
static int op_div(VM* vm, const DecodedInst* di){
u32 ua, ub;
    if(!read_operand_u32(vm, &di->A, &ua)) return -1;
    if(!read_operand_u32(vm, &di->B, &ub)) return -1;

    if(ub == 0){
        fprintf(stderr, "Error: división por cero\n");
        return -1;
    }
    int32_t a = (int32_t)ua;
    int32_t b = (int32_t)ub;

    int32_t q = a / b;     
    int32_t r = a % b;    
    if (r != 0) {
        if ((r < 0 && b > 0) || (r > 0 && b < 0)) {
            q -= 1;
            r += b;
        }
    }
    if(!write_operand_u32(vm, &di->A, (u32)q)) return -1;
    vm->reg[AC] = (u32)r;          
    set_NZ(vm, (u32)q);             
    return 0;
}
static int op_cmp(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 res = a - b;
    set_NZ(vm, res);
    return 0;
}
static int op_and(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 res = a & b;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    set_NZ(vm, res);
    return 0;
}
static int op_or(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 res = a | b;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    set_NZ(vm, res);
    return 0;
}
static int op_xor(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 res = a ^ b;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    set_NZ(vm, res);
    return 0;
}
static int op_not(VM* vm, const DecodedInst* di){
    u32 a;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    u32 res = ~a;
    if(!write_operand_u32(vm, &di->A, res)) return -1;
    set_NZ(vm, res);
    return 0;
}
static int op_shl(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 r=(u32)(a << shamt32(b));
    if(!write_operand_u32(vm, &di->A, r)) return -1;
    set_NZ(vm, r);
    return 0;
}
static int op_shr(VM* vm, const DecodedInst* di){
    u32 a, b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    u32 r=(u32)((u32)a >> shamt32(b));
    if(!write_operand_u32(vm, &di->A, r)) return -1;
    set_NZ(vm, r);
    return 0;
}
static int op_sar(VM* vm, const DecodedInst* di){
    u32 a,b;
    if(!read_operand_u32(vm, &di->A, &a)) return -1;
    if(!read_operand_u32(vm, &di->B, &b)) return -1;
    int32_t r = (int32_t)a>>shamt32(b);
    if(!write_operand_u32(vm, &di->A, (u32)r)) return -1;
    set_NZ(vm, (u32)r);
    return 0;
}
static bool read_jump_offset(VM* vm, const DecodedInst* di, int16_t* off){
    u32 v;
    if(!read_operand_u32(vm, &di->A, &v)) return false;
    *off = (int16_t)(v & 0xFFFFu);
    return true;
}
static int op_jmp(VM* vm, const DecodedInst* di){
    int16_t off;
    if(!read_jump_offset(vm, di, &off)) return -1;
    jump_to_code(vm, (uint16_t)off);
    return 0;
}
static int op_jz(VM* vm, const DecodedInst* di){
    int16_t off;
    if(!read_jump_offset(vm, di, &off)) return -1;
    if(cc_Zbit(vm)){
        jump_to_code(vm, (uint16_t)off);
    }
    return 0;
}
static int op_jnz(VM* vm, const DecodedInst* di){
    int16_t off;
    if(!read_jump_offset(vm, di, &off)) return -1;
    if(!cc_Zbit(vm)){
        jump_to_code(vm, (uint16_t)off);
    }
    return 0;
}
static int op_jn(VM* vm, const DecodedInst* di){
    int16_t off;
    if(!read_jump_offset(vm, di, &off)) return -1;
    if(cc_Nbit(vm)){
        jump_to_code(vm, (uint16_t)off);
    }
    return 0;
}
static int op_jnn(VM* vm, const DecodedInst* di){
    int16_t off;
    if(!read_jump_offset(vm, di, &off)) return -1;
    if(!cc_Nbit(vm)){
        jump_to_code(vm, (uint16_t)off);
    }
    return 0;
}
static int op_jp(VM* vm, const DecodedInst* di){
    int16_t off;
    if(!read_jump_offset(vm, di, &off)) return -1;
    if(!cc_Nbit(vm) && !cc_Zbit(vm)){
        jump_to_code(vm, (uint16_t)off);
    }
    return 0;
}
static int op_jnp(VM* vm, const DecodedInst* di){
    int16_t off;
    if(!read_jump_offset(vm, di, &off)) return -1;
    if (cc_Nbit(vm) || cc_Zbit(vm)){
        jump_to_code(vm, (uint16_t)off);
    }
    return 0;
}
static int op_swap(VM* vm, const DecodedInst* di){
    if(di->A.type == OT_IMM || di->B.type == OT_IMM || di->A.type == OT_NONE || di->B.type == OT_NONE)
        return -1;
    u32 va, vb;
    if(!read_operand_u32(vm, &di->A, &va)) return -1;
    if(!read_operand_u32(vm, &di->B, &vb)) return -1;
    if(!write_operand_u32(vm, &di->A, vb)) return -1;
    if(!write_operand_u32(vm, &di->B, va)) return -1;
    return 0;

}
static int op_rnd(VM* vm, const DecodedInst* di){
    static int seeded=0;
    if(!seeded){ srand((unsigned)time(NULL)); seeded=1; }
    
    u32 lim;
    if(!read_operand_u32(vm, &di->B, &lim)) return -1;
    u32 val=(lim==0u)?0:(u32)(rand() % lim);
    if(!write_operand_u32(vm, &di->A, val)) return -1;
    set_NZ(vm, val);
    return 0;
}
static int op_push(VM* vm, const DecodedInst* di){
    uint32_t v = 0;
    if (!read_operand_u32(vm, &di->A, &v)) {
        v = 0;
    }
    if (stack_push32(vm, v) < 0) {
        return -1;
    }
    return 0;
}

static int op_pop(VM* vm, const DecodedInst* di){
    uint32_t v=0;
    if (di->A.type==OT_NONE || di->A.type==OT_IMM) return -1;
    if (stack_pop32(vm, &v) < 0) return -1;
    return write_operand_u32(vm, &di->A, v);
}
static int op_call(VM* vm, const DecodedInst* di){
    int16_t off;
    if (!read_jump_offset(vm, di, &off)) return -1; 
    if (stack_push32(vm, vm->reg[IP]) < 0) return -1;
    jump_to_code(vm, (uint16_t)off);
    return 0;
}
static int op_ret(VM* vm, const DecodedInst* di){
    (void)di;
    uint32_t ret;
    if (stack_pop32(vm, &ret) < 0) return -1;
    vm->reg[IP] = ret;   
    return 0;
}
static bool read_line(char* buf, size_t cap){
    if (!fgets(buf, (int)cap, stdin)) {
        clearerr(stdin);
        return false;
    }
    size_t n = strlen(buf);
    while (n && (buf[n-1]=='\n' || buf[n-1]=='\r')) {
        buf[--n] = 0;
    }
    return true;                     
}
static bool parse_input(u32 mode, u16 cell_size, u32* out){
    char buf[256];
    if (!read_line(buf, sizeof buf)) {
        fprintf(stderr, "Error: Falla al leer input de SYS 1. Abortando.\n");
        return false;
    }

    if (mode & MODE_CHR){
        u32 v = 0;
        for (u16 i = 0; i < cell_size; i++){
            unsigned char ch = (unsigned char)(buf[i] ? buf[i] : 0);
            v = (v << 8) | (u32)ch;
        }
        *out = v;
        return true;
    }

    int base = 10;
    if      (mode & MODE_BIN) base = 2;
    else if (mode & MODE_OCT) base = 8;
    else if (mode & MODE_HEX) base = 16;

    char* p = buf;
    if (p[0]=='0' && (p[1]=='x'||p[1]=='X')) { p += 2; base = 16; }
    else if (p[0]=='0' && (p[1]=='b'||p[1]=='B')) { p += 2; base = 2; }
    else if ((mode & MODE_OCT) && p[0]=='0') {
    }

    unsigned long ul = strtoul(p, NULL, base);
    *out = (u32)ul;
    return true;
}
static void print_binary(u32 v){
    if (v == 0){ printf("0b0"); return; }
    printf("0b");
    int started = 0;
    for (int i = 31; i >= 0; --i){
        int bit = (v >> i) & 1;
        if (!started && bit == 0) continue;  
        started = 1;
        putchar(bit ? '1' : '0');
    }
}
static inline uint32_t mask_by_size(uint32_t v, uint16_t sz){
    switch (sz){
        case 1: return v & 0xFFu;
        case 2: return v & 0xFFFFu;
        case 4: default: return v;
    }
}
static void print_hex_padded(uint32_t v, uint16_t cell_size){
    switch (cell_size){
        case 1: printf("0x%X",  (unsigned)(v & 0xFFu));    break;
        case 2: printf("0x%X",  (unsigned)(v & 0xFFFFu));  break;
        case 4: printf("0x%X",  (unsigned)v);              break;
        default: printf("0x%X", (unsigned)v);              break;
    }
}
static void print_dec_signed(uint32_t v, uint16_t cell_size){
    switch (cell_size){
        case 1:  printf("%d",  (int8_t) (v & 0xFFu));    break;
        case 2:  printf("%d", (int16_t)(v & 0xFFFFu));   break;
        case 4:  printf("%d", (int32_t) v);              break;
        default: printf("%u",  v);                       break;
    }
}
static void print_chars(u32 v, u16 size){
    for(int i=size-1; i>=0; --i){
        unsigned char ch = (unsigned char)((v >> (i*8)) & 0xFFu);
        putchar(isprint(ch)?ch:'.');
    }
}
static void print_cell(uint32_t modes, uint32_t value, uint16_t cell_size){
    uint32_t shown = mask_by_size(value, cell_size);
    int first = 1;

    if (modes & MODE_HEX){
        if (!first) putchar(' ');
        print_hex_padded(shown, cell_size);
        first = 0;
    }
    if (modes & MODE_OCT){
        if (!first) putchar(' ');
        printf("0o%o", (unsigned)shown);
        first = 0;
    }
    if (modes & MODE_CHR){
        if (!first) putchar(' ');
        print_chars(shown, cell_size);
        first = 0;
    }
    if (modes & MODE_DEC){
        if (!first) putchar(' ');
        print_dec_signed(shown, cell_size);
        first = 0;
    }
    if (modes & MODE_BIN){
        if (!first) putchar(' ');
        print_binary(shown);
    }
}

static int op_sys(VM* vm, const DecodedInst* di){
    uint32_t callno = 0xFFFFFFFFu;
    read_operand_u32(vm, &di->A, &callno);

    if (!read_operand_u32(vm, &di->A, &callno)) {
        callno = (vm->reg[EAX] & 0xFFFFu);
    }

    uint32_t eax = vm->reg[EAX];
    uint32_t ecx = vm->reg[ECX];
    uint32_t edx = vm->reg[EDX];

    uint16_t count = ecx_count(ecx);  
    uint16_t size  = ecx_size(ecx);   

    if (callno == 1u) {
     uint32_t modes = eax & 0x1Fu;

     if (count == 0) {count = 1;}
     if (size == 0) { size = 4;}

     for (uint16_t i = 0; i < count; ++i) {
        uint16_t phys;
        if (!phys_of_cell(vm, edx, size, i, &phys))
            return -1;

        printf("[%04X]: ", phys);
        fflush(stdout);

        uint32_t val = 0;
        if (!parse_input(modes, size, &val))
            return -1;

        uint16_t seg = (uint16_t)(edx >> 16);
        uint16_t off = (uint16_t)((edx & 0xFFFFu) + i * size);
        if (!mem_write_cell(vm, seg, off, size, val))
            return -1;
     }
     return 0;
    }

    if (callno == 2u){
        uint32_t modes = eax & 0x1Fu;
        for (uint16_t i=0; i<count; ++i){
            uint16_t phys;
            if (!phys_of_cell(vm, edx, size, i, &phys)) return -1;

            uint16_t seg = (uint16_t)(edx >> 16);
            uint16_t off = (uint16_t)((edx & 0xFFFFu) + i*size);
            uint32_t val = 0;
            if (!mem_read_cell(vm, seg, off, size, &val)) return -1;

            printf("[%04X]: ", phys);
            print_cell(modes, val, size);
            putchar('\n'); fflush(stdout);
        }
        return 0;
    }

    if (callno == 3u){
        uint16_t maxlen = (uint16_t)(ecx & 0xFFFFu);
        uint16_t seg = (uint16_t)(edx >> 16);
        uint16_t off = (uint16_t)(edx & 0xFFFFu);
        if (maxlen == 0) return 0;

        char buf[1024];
        if (!read_line(buf, sizeof buf)) buf[0] = 0;
        size_t n = strlen(buf);
        if (n > (size_t)(maxlen-1)) n = (size_t)(maxlen-1);

        for (size_t i=0; i<n; ++i){
            if (!mem_write_u8(vm, seg, (uint16_t)(off + (uint16_t)i), (uint32_t)(uint8_t)buf[i])) return -1;
        }
        if (!mem_write_u8(vm, seg, (uint16_t)(off + (uint16_t)n), 0)) return -1;
        return 0;
    }

    if (callno == 4u){
        uint16_t seg = (uint16_t)(edx >> 16);
        uint16_t off = (uint16_t)(edx & 0xFFFFu);
        for (;;){
            uint32_t ch;
            if (!mem_read_u8(vm, seg, off, &ch)) break;
            off++;
            if (ch == 0) break;
            putchar((int)(uint8_t)ch);
        }
        fflush(stdout);
        return 0;
    }

    if (callno == 7u){
        term_clear();
        return 0;
    }

    if (callno == 0xFu){
        if (!vm->have_vmi || !vm->opt_vmi_path){
            return 0;
        }
        if (!vm_save_vmi(vm, vm->opt_vmi_path)) return -1;

        for(;;){
            fputs("breakpoint (g=go, ENTER=step, q=quit)> ", stdout); fflush(stdout);
            char line[64]; if (!fgets(line, sizeof line, stdin)) line[0]=0;
            size_t m=strlen(line);
            while (m && (line[m-1]=='\n' || line[m-1]=='\r')) line[--m]=0;

            if (line[0] == 'g' || line[0] == 'G'){
                return 0;              
            } else if (line[0] == 'q' || line[0] == 'Q'){
                vm->reg[IP] = 0xFFFFFFFFu;
                return -1;
            } else {
                if (single_step(vm) < 0) return -1;
                if (!vm_save_vmi(vm, vm->opt_vmi_path)) return -1;
            }
        }
    }
    return -1;
}

void init_dispatch_table(OpHandler tb[256]){
    for(int i=0; i<256; i++) tb[i]=op_invalid;

    tb[0x00] = op_sys;
    tb[0x01] = op_jmp;
    tb[0x02] = op_jz;
    tb[0x03] = op_jp;
    tb[0x04] = op_jn;
    tb[0x05] = op_jnz;
    tb[0x06] = op_jnp;
    tb[0x07] = op_jnn;
    tb[0x08] = op_not;

    tb[0x0B] = op_push; 
    tb[0x0C] = op_pop;  
    tb[0x0D] = op_call;  
    tb[0x0E] = op_ret;   
    tb[0x0F] = op_stop; 

    tb[0x10] = op_mov;
    tb[0x11] = op_add;
    tb[0x12] = op_sub;
    tb[0x13] = op_mul;
    tb[0x14] = op_div;
    tb[0x15] = op_cmp;
    tb[0x16] = op_shl;
    tb[0x17] = op_shr;
    tb[0x18] = op_sar;
    tb[0x19] = op_and;
    tb[0x1A] = op_or;
    tb[0x1B] = op_xor;
    tb[0x1C] = op_swap;
    tb[0x1D] = op_ldl;
    tb[0x1E] = op_ldh;
    tb[0x1F] = op_rnd;
}

int exec_instruction(VM* vm, const DecodedInst* di, OpHandler tb[256]){
    return tb[di->opcode](vm, di);
}