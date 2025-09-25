#include "decoder.h"
#include "disasm.h"
#include "memory.h"
#include "vm.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static inline uint8_t vmx_lo(uint8_t b){ return (uint8_t)(b & 0x0F); }
const char* opcode_mnemonic(u8 op){
    static const char* T[256]={
        [0x00] = "SYS", 
        [0x01] = "JMP", 
        [0x02] = "JZ",  
        [0x03] = "JP",
        [0x04] = "JN",
        [0x05] = "JNZ",
        [0x06] = "JNP",
        [0x07] = "JNN",
        [0x08]="NOT", 
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

static const char* vmx_regname(uint8_t code){
    switch (code){
        case 0x00: return "LAR";
        case 0x01: return "MAR";
        case 0x02: return "MBR";
        case 0x03: return "IP";
        case 0x04: return "OPC";
        case 0x05: return "OP1";
        case 0x06: return "OP2";
        /* 0x07..0x09 reservados */
        case 0x0A: return "EAX";
        case 0x0B: return "EBX";
        case 0x0C: return "ECX";
        case 0x0D: return "EDX";
        case 0x0E: return "EEX";
        case 0x0F: return "EFX";
        case 0x10: return "AC";
        case 0x11: return "CC";
        /* 0x12..0x19 reservados */
        case 0x1A: return "CS";
        case 0x1B: return "DS";
        /* 0x1C..0x1F reservados */
        default:   return "R?";
    }
}


void format_operand(const DecodedOp* op, char* out, size_t cap){
    if (!op || !out || !cap) return;
    out[0] = 0;

    switch (op->type) {
    case OT_REG: {
        uint8_t code = (uint8_t)(op->raw[0]& 0x1F);  
        snprintf(out, cap, "%s", vmx_regname(code));
        return;
    }
    case OT_IMM: {
        uint16_t v = be16(op->raw[0], op->raw[1]);    // BE → decimal (sin signo)
        snprintf(out, cap, "%u", (unsigned)v);
        return;
    }
    case OT_MEM: {
        uint8_t  b    = op->raw[0]& 0x1F;
        int16_t  disp = (int16_t)((op->raw[1]<<8) | op->raw[2]); // BE con signo
        const char* base = vmx_regname(b);

        if      (disp == 0) snprintf(out, cap, "[%s]", base);
        else if (disp < 0)  snprintf(out, cap, "[%s-%d]", base, -(int)disp);
        else                snprintf(out, cap, "[%s+%d]", base,  (int)disp);
        return;
    }
    default:
        return; // OT_NONE u otro: deja out=""
    }
}

void disasm_print(VM* vm, const DecodedInst* di){
    // Dirección física
    printf("[%04X] ", di->phys);

    // Bytes crudos (usa di->size ya calculado por el decoder)
    uint8_t buf[8];
    uint16_t n = di->size;
    if (n > sizeof(buf)) n = sizeof(buf);
    code_read_bytes(vm, di->phys, buf, n);
    for (uint16_t i = 0; i < n; ++i){
        printf("%02X%s", buf[i], (i+1<n ? " " : ""));
    }
    // separador
    printf(" | ");

    // Mnemónico y operandos
    char A[64]={0}, B[64]={0};
    format_operand(&di->A, A, sizeof(A));
    format_operand(&di->B, B, sizeof(B));

    const char* m = opcode_mnemonic(di->opcode);
    if (di->A.type != OT_NONE && di->B.type != OT_NONE) {
        printf("%s %s, %s\n", m, A, B);
    } else if (di->A.type != OT_NONE) {
        printf("%s %s\n", m, A);
    } else {
        printf("%s\n", m);
    }
}
