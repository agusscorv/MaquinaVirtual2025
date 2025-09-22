#include "cpu.h"
#include "decoder.h"
#include "memory.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <ctype.h>

static inline uint16_t hi16_u32(uint32_t x){ return (uint16_t)(x >> 16); }
static inline uint16_t lo16_u32(uint32_t x){ return (uint16_t)(x & 0xFFFFu); }
static inline uint32_t shamt32(uint32_t v){ return v & 31u; } // 0..31
static inline uint32_t cc_Nbit(VM* vm){ return (vm->reg[CC] >> 31) & 1u; }
static inline uint32_t cc_Zbit(VM* vm){ return (vm->reg[CC] >> 30) & 1u; }
static inline uint16_t ecx_count(uint32_t ecx){ return (uint16_t)(ecx & 0xFFFFu); }
static inline uint16_t ecx_size (uint32_t ecx){ return (uint16_t)(ecx >> 16); }  
enum { MODE_DEC=0x01, MODE_CHR=0x02, MODE_OCT=0x04, MODE_HEX=0x08, MODE_BIN=0x10 };
static inline void jump_to_code(VM* vm, uint16_t off){
  uint16_t code_seg = hi16_u32(vm->reg[CS]);               
  vm->reg[IP] = ((uint32_t)code_seg << 16) | (uint32_t)off; 
}
static inline uint8_t vmxcode_from_byte(uint8_t b){
    uint8_t hi = (uint8_t)(b >> 4);
    uint8_t lo = (uint8_t)(b & 0x0F);
    return (hi != 0) ? hi : lo;
}

static inline uint8_t vm_regidx_from_vmxcode(uint8_t code){
    switch (code){
        case  1: return DS;
        case 10: return EAX;
        case 11: return EBX;
        case 12: return ECX;
        case 13: return EDX;
        case 14: return CS;
        case  0: return LAR;
        case  2: return MBR;
        case  3: return IP;
        case  4: return OPC;
        case  5: return OP1;
        case  6: return OP2;
        case  7: return AC;
        case  8: return CC;
        default: return code; 
    }
}
static bool get_mem_address(VM* vm, const DecodedOp* op, u16* seg_idx, u16* offset){
    if (op->type != OT_MEM) return false;

  
    int16_t disp = be16s(op->raw[1], op->raw[2]);  


    uint8_t b = op->raw[0];


    if (b == 0xF0 || b == 0x0F){
        uint32_t base_ptr = vm->reg[DS];
        *seg_idx = hi16_u32(base_ptr);
        *offset  = (u16)(lo16_u32(base_ptr) + disp);
        return true;
    }


    uint8_t hi   = (uint8_t)(b >> 4);
    uint8_t lo   = (uint8_t)(b & 0x0F);
    uint8_t code = (hi != 0) ? hi : lo;


    uint8_t r = vm_regidx_from_vmxcode(code);
    if (r >= REG_COUNT) return false;


    uint32_t base_ptr = vm->reg[r];
    *seg_idx = hi16_u32(base_ptr);
    *offset  = (u16)(lo16_u32(base_ptr) + disp);
    return true;
}

bool read_operand_u32(VM* vm, const DecodedOp* op, uint32_t* out){
    switch(op->type){
        case OT_NONE:
            *out = 0; return true;
        case OT_REG: {
            uint8_t code = vmxcode_from_byte(op->raw[0]);
            uint8_t r    = vm_regidx_from_vmxcode(code);
            if (r >= REG_COUNT) return false;
            *out = vm->reg[r];
            return true;
        }
        case OT_IMM: {
            *out = (u32)be16(op->raw[0], op->raw[1]);
            return true;
        }
        case OT_MEM: {
            u16 seg, off;
            if(!get_mem_address(vm, op, &seg, &off)) return false;
            return mem_read_u32(vm, seg, off, out);
        }

    }
    return false;
}
bool write_operand_u32(VM* vm, const DecodedOp* op, uint32_t val){
    switch(op->type){
        case OT_REG: {
            uint8_t code = vmxcode_from_byte(op->raw[0]);
            uint8_t r    = vm_regidx_from_vmxcode(code);
            if (r >= REG_COUNT) return false;
            vm->reg[r] = val;
            return true;
        }
        case OT_MEM: {
            u16 seg, off;
            if(!get_mem_address(vm, op, &seg, &off)) return false;
            return mem_write_u32(vm, seg, off, val);
        }
        case OT_NONE:
        case OT_IMM:
        default:
            return false;
    }
}
static bool phys_of_cell(VM* vm, u32 base_ptr, u16 cell_size, u16 idx, u16* out_phys){
    u16 seg=hi16_u32(base_ptr);
    u16 off=(u16)(lo16_u32(base_ptr) + (idx * cell_size));
    return translate_and_check(vm, seg, off, cell_size, out_phys);
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
    u16 off;
    if(!read_jump_offset(vm, di, (int16_t*)&off)) return -1;
    jump_to_code(vm, off);
    return 0;
}
static int op_jz(VM* vm, const DecodedInst* di){
    u16 off;
    if(!read_jump_offset(vm, di, (int16_t*)&off)) return -1;
    if(cc_Zbit(vm)){
        jump_to_code(vm, off);
    }
    return 0;
}
static int op_jnz(VM* vm, const DecodedInst* di){
    u16 off;
    if(!read_jump_offset(vm, di, (int16_t*)&off)) return -1;
    if(!cc_Zbit(vm)){
        jump_to_code(vm, off);
    }
    return 0;
}
static int op_jn(VM* vm, const DecodedInst* di){
    u16 off;
    if(!read_jump_offset(vm, di, (int16_t*)&off)) return -1;
    if(cc_Nbit(vm)){
        jump_to_code(vm, off);
    }
    return 0;
}
static int op_jnn(VM* vm, const DecodedInst* di){
    u16 off;
    if(!read_jump_offset(vm, di, (int16_t*)&off)) return -1;
    if(!cc_Nbit(vm)){
        jump_to_code(vm, off);
    }
    return 0;
}
static int op_jp(VM* vm, const DecodedInst* di){
    u16 off;
    if(!read_jump_offset(vm, di, (int16_t*)&off)) return -1;
    if(!cc_Nbit(vm) && !cc_Zbit(vm)){
        jump_to_code(vm, off);
    }
    return 0;
}
static int op_jnp(VM* vm, const DecodedInst* di){
    u16 off;
    if(!read_jump_offset(vm, di, (int16_t*)&off)) return -1;
    if (cc_Nbit(vm) || cc_Zbit(vm)){
        jump_to_code(vm, off);
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

static bool read_line(char* buf, size_t cap){
    if (!fgets(buf, (int)cap, stdin))  
        return false;

    size_t n = strlen(buf);
    while (n && (buf[n-1]=='\n' || buf[n-1]=='\r')) {
        buf[--n] = 0;
    }
    return true;                     
}

static bool parse_input(u32 mode, u16 cell_size, u32* out){
    char buf[256];
    if (!read_line(buf, sizeof buf))   
        return false;

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

static void print_chars(u32 v, u16 size){
    for(int i=size-1; i>=0; --i){
        unsigned char ch = (unsigned char)((v >> (i*8)) & 0xFFu);
        putchar(isprint(ch)?ch:'.');
    }
}
static void print_cell(u32 modes,u32 value, u16 cell_size){
    int first=1;
    if(modes & MODE_BIN){ if(!first) putchar(' '); print_binary(value); first=0;}
    if(modes & MODE_HEX){ if(!first) putchar(' '); printf("0x%X", value); first=0;}
    if(modes & MODE_OCT){ if(!first) putchar(' '); printf("0o%o", value); first=0;}
    if(modes & MODE_DEC){ if(!first) putchar(' '); printf("%u", value); first=0;}
    if(modes & MODE_CHR){ if(!first) putchar(' '); print_chars(value, cell_size); first=0;}
}


static int op_sys(VM* vm, const DecodedInst* di){
    uint32_t callno;
    if (!read_operand_u32(vm, &di->A, &callno)) return -1;

    uint32_t eax = vm->reg[EAX];                
    uint32_t ecx = vm->reg[ECX];               
    uint32_t edx = vm->reg[EDX];               

    uint16_t count = ecx_count(ecx);            
    uint16_t size  = ecx_size(ecx);            

    if (!(size==1 || size==2 || size==4)) return -1;
    if (callno == 1u){                          
        uint32_t modes = eax & 0x1Fu;

        for (uint16_t i=0; i<count; ++i){       
            uint16_t phys;
            if (!phys_of_cell(vm, edx, size, i, &phys)) return -1;

            printf("[%04X]: ", phys); fflush(stdout);

            uint32_t val = 0;
            if (!parse_input(modes, size, &val)) return -1;

            uint16_t seg = (uint16_t)(edx >> 16);
            uint16_t off = (uint16_t)((edx & 0xFFFFu) + i*size);
            if (!mem_write_cell(vm, seg, off, size, val)) return -1;
        }
        return 0;
    } else if (callno == 2u){               
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
    return -1;  
}



void init_dispatch_table(OpHandler tb[256]){
    for(int i=0; i<256; i++){
        tb[i]=op_invalid;
    }
    tb[0x00] = op_sys;   // SYS
    tb[0x01] = op_jmp;  // JMP
    tb[0x02] = op_jz;   // JZ
    tb[0x03] = op_jp;   // JP (>0)
    tb[0x04] = op_jn;   // JN (<0)
    tb[0x05] = op_jnz;  // JNZ
    tb[0x06] = op_jnp;  // JNP (<=0)
    tb[0x07] = op_jnn;  // JNN (>=0)
    tb[0x08] = op_not;  // NOT 
    tb[0x0F]=op_stop;   // STOP

    tb[0x10] = op_mov;  // MOV 
    tb[0x11] = op_add;  // ADD
    tb[0x12] = op_sub;  // SUB
    tb[0x13] = op_mul;  // MUL
    tb[0x14] = op_div;  // DIV
    tb[0x15] = op_cmp;  // CMP    
    tb[0x16] = op_shl;  // SHL
    tb[0x17] = op_shr;  // SHR
    tb[0x18] = op_sar;  // SAR    
    tb[0x19] = op_and;  // AND
    tb[0x1A] = op_or;   // OR
    tb[0x1B] = op_xor;  // XOR
    tb[0x1C] = op_swap; // SWAP
    tb[0x1D] = op_ldl;  // LDL
    tb[0x1E] = op_ldh;  // LDH
    tb[0x1F] = op_rnd;  // RND


}
int exec_instruction(VM* vm, const DecodedInst* di, OpHandler tb[256]){
    return tb[di->opcode](vm, di);
}
