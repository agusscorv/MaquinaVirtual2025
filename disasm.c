#include "decoder.h"
#include "disasm.h"
#include "memory.h"
#include "vm.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>


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

static inline const char* vmx_regname(uint8_t code){
    switch (code){
        case  1: return "DS";
        case 10: return "EAX";
        case 11: return "EBX";
        case 12: return "ECX";
        case 13: return "EDX";
        case 14: return "CS";
        case  0: return "LAR";
        case  2: return "MBR";
        case  3: return "IP";
        case  4: return "OPC";
        case  5: return "OP1";
        case  6: return "OP2";
        case  7: return "AC";
        case  8: return "CC";
        default:{
            static char buf[8];
            snprintf(buf, sizeof buf, "R%u", code);
            return buf;
        }
    }
}

void format_operand(const DecodedOp* op, char* out, size_t cap){
  if (!op || !out || !cap) return;

  switch (op->type) {
    case OT_REG: {
      uint8_t code = (uint8_t)(op->raw[0] & 0x0F);
      snprintf(out, cap, "%s", vmx_regname(code)); 
      return;
    }
    case OT_IMM: {
      uint16_t v = be16(op->raw[0], op->raw[1]);  
      snprintf(out, cap, "%u", (unsigned)v);
      return;
    }
    case OT_MEM: {
      uint8_t  b    = op->raw[0];
      int16_t  disp = be16s(op->raw[1], op->raw[2]);

      if (b == 0x0F || b == 0xF0){
        const char* rb = "DS";
        if (disp == 0)           snprintf(out, cap, "[%s]", rb);
        else if (disp < 0)       snprintf(out, cap, "[%s-%d]", rb, -(int)disp);
        else                     snprintf(out, cap, "[%s+%d]", rb,  (int)disp);
        return;
      }

      uint8_t  code = (uint8_t)(b & 0x0F);
      const char* rb = vmx_regname(code);
      if (disp == 0)           snprintf(out, cap, "[%s]", rb);
      else if (disp < 0)       snprintf(out, cap, "[%s-%d]", rb, -(int)disp);
      else                     snprintf(out, cap, "[%s+%d]", rb,  (int)disp);
       }
      default: out[0]=0; return;
     }
}

void disasm_print(VM* vm, const DecodedInst* di){
    printf("[%04X] ", di->phys);

    u8 buf[8];
    u16 n= di->size;
    if(n > sizeof(buf)) n = sizeof(buf);
    code_read_bytes(vm, di->phys, buf, n);
    for(u16 i=0; i<n; i++){
        printf("%02X", buf[i]);
        if(i+1 < n) putchar(' ');
    }
    for(u16 i=n; i<5; i++){
        printf(" ");
    }
    char A[64], B[64];
    format_operand(&di->A, A, sizeof(A));
    format_operand(&di->B, B, sizeof(B));

    const char* m = opcode_mnemonic(di->opcode);
    printf(" | %s", m);

    int hasA = (di->A.type != OT_NONE);
    int hasB = (di->B.type != OT_NONE);

    if(hasA && hasB){
        printf(" %s, %s", A, B);
    }else if(hasA){
        printf(" %s", A);
    }
    putchar('\n');  
}