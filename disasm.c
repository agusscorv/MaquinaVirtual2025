#include "decoder.h"
#include "disasm.h"
#include "memory.h"
#include "vm.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static inline int is_ds_implicit_u8(uint8_t b){ return (b==0x0F || b==0xF0); }
static const char* vmx_regname(uint8_t code){
    switch (code){
        case 0x00: return "LAR";  
        case 0x01: return "MAR";  
        case 0x02: return "MBR";
        case 0x03: return "IP";   
        case 0x04: return "OPC";  
        case 0x05: return "OP1";
        case 0x06: return "OP2"; 
        case 0x07: return "SP";  
        case 0x08: return "BP"; 
        case 0x0A: return "EAX";  
        case 0x0B: return "EBX";
        case 0x0C: return "ECX";  
        case 0x0D: return "EDX";  
        case 0x0E: return "EEX";
        case 0x0F: return "EFX";  
        case 0x10: return "AC";   
        case 0x11: return "CC";
        case 0x1A: return "CS";   
        case 0x1B: return "DS";   
        case 0x1C: return "ES";
        case 0x1D: return "SS";   
        case 0x1E: return "KS";   
        case 0x1F: return "PS";
        default: return "R?";
    }
}

static const char* logical_kind_for_index(VM* vm, int i){
    if (i == vm->idx_param) return "PARAM";
    if (i == vm->idx_const) return "CONST";
    if (i == vm->idx_code)  return "CODE";
    if (i == vm->idx_data)  return "DATA";
    if (i == vm->idx_extra) return "EXTRA";
    if (i == vm->idx_stack) return "STACK";
    return "?";
}

void disasm_dump_segments(VM* vm){
    puts("SEGMENTS:");
    for (int i = 0; i < SEG_COUNT; i++){
        u16 base = vm->seg[i].base;
        u16 size = vm->seg[i].size;
        if (size == 0) continue;
        printf(" %d %-6s base=%04X size=%04X\n",
               i,
               logical_kind_for_index(vm, i),
               (unsigned)base,
               (unsigned)size);
    }

    u16 cs_idx = (u16)(vm->reg[CS] >> 16);
    u16 ip_off = (u16)(vm->reg[IP] & 0xFFFFu);
    printf("ENTRY: CS=%u IP=%04X\n\n",
           (unsigned)cs_idx,
           (unsigned)ip_off);
}

static void print_string_preview_bytes(const u8* p, u16 length){
    u16 max = (length > 16) ? 16 : length;
    for (u16 i = 0; i < max; i++){
        u8 ch = p[i];
        if (ch == 0) break;
        printf("%02X ", (unsigned)ch);
    }
}


static void print_string_preview_text(const u8* p, u16 length){
    putchar('"');
    for (u16 i = 0; i < length; i++){
        u8 ch = p[i];
        if (ch == 0) break;
        putchar((ch >= 32 && ch < 127) ? ch : '.');
    }
    putchar('"');
}

void disasm_dump_const_strings(VM* vm){
    if (vm->idx_const < 0) {
        return;
    }

    u16 base = vm->seg[vm->idx_const].base;
    u16 size = vm->seg[vm->idx_const].size;
    if (size == 0) return;

    puts("CONST STRINGS:");

    u16 off = 0;
    while (off < size){
        while (off < size && vm->ram[base + off] == 0){
            off++;
        }
        if (off >= size) break;

        u16 start = off;
        while (off < size && vm->ram[base + off] != 0){
            off++;
        }
        u16 length = (u16)(off - start);

        printf(" [%04X] ", (unsigned)(base + start));
        print_string_preview_bytes(&vm->ram[base + start], length);
        printf("| ");
        print_string_preview_text(&vm->ram[base + start], length);
        putchar('\n');

        if (off < size && vm->ram[base + off] == 0){
            off++;
        }
    }

    putchar('\n');
}

static const char* reg_aliased_name(uint8_t reg_code, uint8_t sector){
    switch (reg_code) {
        case 0x0A: return (const char*[]){"EAX","AX","AH","AL"}[sector];
        case 0x0B: return (const char*[]){"EBX","BX","BH","BL"}[sector];
        case 0x0C: return (const char*[]){"ECX","CX","CH","CL"}[sector];
        case 0x0D: return (const char*[]){"EDX","DX","DH","DL"}[sector];
        case 0x0E: return (const char*[]){"EEX","EX","EH","EL"}[sector];
        case 0x0F: return (const char*[]){"EFX","FX","FH","FL"}[sector];
        default:   return vmx_regname(reg_code);
    }
}
const char* opcode_mnemonic(u8 op){
    static const char* T[256]={
        [0x00]="SYS",
        [0x01]="JMP",
        [0x02]="JZ",
        [0x03]="JP",
        [0x04]="JN",
        [0x05]="JNZ",
        [0x06]="JNP",
        [0x07]="JNN",
        [0x08]="NOT",

        [0x0B]="PUSH",
        [0x0C]="POP",
        [0x0D]="CALL",
        [0x0E]="RET",
        [0x0F]="STOP",

        [0x10]="MOV",
        [0x11]="ADD",
        [0x12]="SUB",
        [0x13]="MUL",
        [0x14]="DIV",
        [0x15]="CMP",
        [0x16]="SHL",
        [0x17]="SHR",
        [0x18]="SAR",
        [0x19]="AND",
        [0x1A]="OR",
        [0x1B]="XOR",
        [0x1C]="SWAP",
        [0x1D]="LDL",
        [0x1E]="LDH",
        [0x1F]="RND",
    };
    const char* s = T[op];
    return s ? s : "OP?";
}

static inline uint8_t reg_sector_from_raw0(uint8_t r0){ return (uint8_t)((r0>>5)&0x3); }
static inline uint8_t mem_size_from_raw0(uint8_t r0){
    switch ((r0>>6)&0x3){ case 0: return 4; case 1: return 2; case 2: return 1; default: return 4; }
}
static inline char mem_prefix(uint8_t sz){ return (sz==1)?'b':(sz==2)?'w':'l'; }

static void format_operand(const DecodedOp* op, uint8_t size_hint, char* out, size_t cap){
    if (!op || !out || !cap){ return; }
    out[0] = 0;

    switch (op->type){
    case OT_REG: {
        uint8_t code = (uint8_t)(op->raw[0] & 0x1F);
        if (code >= 0x0A && code <= 0x0F){
            int use_sector = -1;
            if (size_hint == 1)      use_sector = 3; /* 8b low  -> AL/BL/CL/DL/EL/FL */
            else if (size_hint == 2) use_sector = 1; /* 16b     -> AX/BX/CX/DX/EX/FX */
            else if (size_hint == 4) use_sector = 0; /* 32b     -> EAX/EBX/...       */

            if (use_sector < 0){
                use_sector = (int)((op->raw[0] >> 5) & 0x3); 
            }

            snprintf(out, cap, "%s", reg_aliased_name(code, (uint8_t)use_sector));
        } else {
            snprintf(out, cap, "%s", vmx_regname(code));
        }
        return;
    }

    case OT_IMM: {
        int16_t sval = (int16_t)be16_pair(op->raw[0], op->raw[1]);
        snprintf(out, cap, "%d", (int)sval);
        return;
    }

    case OT_MEM: {
        uint8_t r0 = op->raw[0];
        int16_t disp = (int16_t)((op->raw[1] << 8) | op->raw[2]);
        uint8_t sz = mem_size_from_raw0(r0);
        char p = (sz==1)?'b':(sz==2)?'w':'l';

        const char* base = is_ds_implicit_u8(r0) ? "DS" : vmx_regname((uint8_t)(r0 & 0x1F));


        if (disp == 0) snprintf(out, cap, "%c[%s]",    p, base);
        else if (disp < 0)  snprintf(out, cap, "%c[%s-%d]", p, base, -(int)disp);
        else snprintf(out, cap, "%c[%s+%d]", p, base,  (int)disp);
        return;
    }

    default:
        return;
    }
}

#define COL_MNEM 4
#define COL_A    18

void disasm_print(VM* vm, const DecodedInst* di){
    static int      have_entry = 0;
    static uint16_t entry_phys = 0;
    if (!have_entry){ entry_phys = di->phys; have_entry = 1; }

    putchar((di->phys == entry_phys)?'>':' ');

    printf("[%04X] ", di->phys);

    uint8_t raw[8];
    uint16_t n = di->size;
    if (n > sizeof(raw)) n = sizeof(raw);
    code_read_bytes(vm, di->phys, raw, n);
    for (uint16_t i=0; i<n; ++i){
        printf("%02X%s", raw[i], (i+1<n ? " " : ""));
    }

    printf(" | ");

    uint8_t hintA = 0, hintB = 0;            /* 0=sin pista, 1/2/4 = b/w/l */
    if (di->A.type == OT_REG && di->B.type == OT_MEM)
        hintA = mem_size_from_raw0(di->B.raw[0]);
    if (di->B.type == OT_REG && di->A.type == OT_MEM)
        hintB = mem_size_from_raw0(di->A.raw[0]);
    char A[64] = {0}, B[64] = {0};
    format_operand(&di->A, hintA, A, sizeof A);
    format_operand(&di->B, hintB, B, sizeof B);

    const char* m = opcode_mnemonic(di->opcode);

    if (di->A.type != OT_NONE && di->B.type != OT_NONE){
        char AwithComma[96];
        snprintf(AwithComma, sizeof AwithComma, "%s,", A); 
        printf("%-*s %-*s %s\n", COL_MNEM, m, COL_A, AwithComma, B);
    } else if (di->A.type != OT_NONE){
        printf("%-*s %s\n", COL_MNEM, m, A);
    } else {
        printf("%s\n", m);
    }
}