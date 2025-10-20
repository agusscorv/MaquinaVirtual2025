// vm.c
#include "cpu.h"
#include "decoder.h"
#include "disasm.h"
#include "memory.h"
#include "vm.h"
#include <stdio.h>
#include <string.h>

static inline u16 read_u16_be(const u8 b[2]){
  return (u16)(b[0] << 8) | b[1];
}

void vm_init(VM* vm, bool disassemble) {
  memset(vm, 0, sizeof(*vm));
  vm->disassemble = disassemble;

}

bool vm_load(VM* vm, const char* path) {
  FILE* f=fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "Error: No se pudo abrir el archivo %s\n", path);
    return false;
  }
  
  u8 hdr[8];
  size_t n=fread(hdr, 1, sizeof(hdr), f);
  if (n!=sizeof(hdr)) {
    fprintf(stderr, "Error: No se pudo leer el encabezado del archivo %s\n", path);
    fclose(f);
    return false;
  }
  if(memcmp(hdr, "VMX25", 5)!=0) {
    fprintf(stderr, "Error: Formato de archivo inválido en %s\n", path);
    fclose(f);
    return false;
  }
  if(hdr[5]!=1){
    fprintf(stderr, "Error: Versión de archivo no soportada en %s\n", path);
    fclose(f);
    return false;
  }
  u16 code_size=read_u16_be(&hdr[6]);
  if (code_size == 0 || code_size > RAM_SIZE) {
    fprintf(stderr, "Error: Tamaño de código inválido %u en %s\n", code_size, path);
    fclose(f);
    return false;
  }
  size_t read_code=fread(vm->ram, 1, code_size, f);
  if (read_code != code_size) {
    fprintf(stderr, "Error: el binario no contiene %u bytes de codigo\n", code_size);
    fclose(f);
    return false;
  }
  fclose(f);

  vm->code_size = code_size;

  vm->seg[0].base = 0;
  vm->seg[0].size = code_size;

  vm->seg[1].base = code_size;
  vm->seg[1].size = (u16)(RAM_SIZE - code_size);

  vm->reg[CS] = 0x00000000u;
  vm->reg[DS] = 0x00010000u;
  vm->reg[IP] = vm->reg[CS];

  vm->reg[OPC] = vm->reg[OP1] = vm->reg[OP2] = 0;
  vm->reg[CC] = 0;

  return true; 
}

int vm_run(VM* vm) {
  OpHandler table[256];
  init_dispatch_table(table);

  for (;;) {
    // 1) Fin normal solo por STOP
    if (vm->reg[IP] == 0xFFFFFFFFu) {
      return 0;
    }

    // 2) Preparar fetch de la instrucción en IP
    u16 seg   = (u16)(vm->reg[IP] >> 16);
    u16 off   = (u16)(vm->reg[IP] & 0xFFFFu);

    // >>> NUEVO: fin de código = término normal (sin error)
    // Si IP está EXACTAMENTE al final del segmento -> terminó el programa.
    // (No confundir con "pasado" el final, que sí es error.)
    if (off == vm->seg[seg].size) {
      return 0; // sin errores
    }
    if (off > vm->seg[seg].size) {
      fprintf(stderr, "Error: instruccion invalida\n");
      return 1;
    }

    u16 phys;
    // Si no puedo ni leer el primer byte y no es fin exacto (ya chequeado arriba),
    // entonces sí es instrucción inválida.
    if (!translate_and_check(vm, seg, off, 1, &phys)) {
      fprintf(stderr, "Error: instruccion invalida\n");
      return 1;
    }

    DecodedInst di;
    if (!fetch_and_decode(vm, &di)) {
      u32 opc = 0xFF;
      (void)mem_read_u8(vm, seg, off, &opc);
      fprintf(stderr, "Error: instruccion invalida OPC=%02X\n", (unsigned)opc);
      return 1;
    }

    if (vm->disassemble) {
      disasm_print(vm, &di);
    }

    int rc = exec_instruction(vm, &di, table);
    if (rc < 0) {
      return 1; // algún op informó error
    }
  }
}
