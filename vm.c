// vm.c
#include "cpu.h"
#include "decoder.h"
#include "disasm.h"
#include "memory.h"
#include "vm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static inline u32 logical_ptr(int seg_idx, u16 off){
  return (seg_idx < 0) ? 0xFFFFFFFFu : (((u32)seg_idx << 16) | (u32)off);
}
static inline u16 place(u16* cursor, u16 size){
  u16 pos = *cursor; *cursor = (u16)(pos + size); return pos;
}
static int ensure_ram_capacity(const VM* vm, u16 needed){
    const u32 limit_bytes = (u32)vm->ram_kib * 1024u;

    if (vm->ram_kib > RAM_DEFAULT_KIB){
        fprintf(stderr,
            "Error: RAM solicitada m=%u KiB excede el máximo compilado (%u KiB).\n",
            (unsigned)vm->ram_kib,
            (unsigned)RAM_DEFAULT_KIB);
        return 0;
    }

    if ((u32)needed > limit_bytes){
        fprintf(stderr,"Error: memoria insuficiente para montar el proceso.\n");
        return 0;
    }

    return 1;
}

static inline void be32w(FILE* f, u32 v){
  u8 b[4] = { (u8)(v>>24), (u8)(v>>16), (u8)(v>>8), (u8)v };
  fwrite(b,1,4,f);
}
static inline void be16w(FILE* f, u16 v){
  u8 b[2] = { (u8)(v>>8), (u8)v };
  fwrite(b,1,2,f);
}
static inline u32 be32p(const u8* p){
  return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3];
}

static int build_param_segment(VM* vm, char** params, int argc, u16 seg_base, u16 seg_size, u16* out_used, u16* out_argv_off){
  u16 cur = 0;
  for (int i=0; i<argc; ++i){
    const char* s = params[i] ? params[i] : "";
    size_t len = strlen(s) + 1;
    if ((u32)cur + (u32)len > (u32)seg_size) return 0;
    memcpy(&vm->ram[seg_base + cur], s, len);
    cur += (u16)len;
  }
  const u16 argv_off = cur;
  const u32 argv_bytes = (u32)argc * 4u;
  if ((u32)cur + argv_bytes > (u32)seg_size) return 0;

  for (int i=0, off=0; i<argc; ++i){
    if (i==0) off = 0;
    else {
      const char* prev = params[i-1] ? params[i-1] : "";
      off += (int)strlen(prev) + 1;
    }
    const u32 p = (((u32)SEG_PARAM) << 16) | (u32)(u16)off;
    vm->ram[seg_base + cur + 0] = (u8)(p >> 24);
    vm->ram[seg_base + cur + 1] = (u8)(p >> 16);
    vm->ram[seg_base + cur + 2] = (u8)(p >> 8);
    vm->ram[seg_base + cur + 3] = (u8)(p);
    cur = (u16)(cur + 4);
  }

  *out_used = cur;
  *out_argv_off = argv_off;
  return 1;
}

void vm_init(VM* vm, bool disassemble) {
    memset(vm, 0, sizeof(*vm));

    vm->disassemble = disassemble;
    vm->ram_kib = RAM_DEFAULT_KIB;

    vm->opt_vmx_path = NULL;
    vm->opt_vmi_path = NULL;
    vm->have_vmx = 0;
    vm->have_vmi = 0;

    vm->have_params = 0;
    vm->argc_on_stack = 0;

    for (int i = 0; i < SEG_COUNT; i++) {
        vm->seg[i].base = 0;
        vm->seg[i].size = 0;
    }

    vm->code_size = 0;

    vm->idx_param = -1;
    vm->idx_const = -1;
    vm->idx_code  = -1;
    vm->idx_data  = -1;
    vm->idx_extra = -1;
    vm->idx_stack = -1;
}


bool vm_save_vmi(VM* vm, const char* path){
    if (!path) return false;

    FILE* g = fopen(path, "wb");
    if (!g){
        fprintf(stderr,"VMI: no pude abrir %s\n", path);
        return false;
    }

    /*Header */
    fwrite("VMI25", 1, 5, g);  
    fputc(1, g);              

    /* Tamaño de RAM en KiB (u16 big-endian) */
    be16w(g, (u16)vm->ram_kib);

    /*Registros (32 x u32 BE) */
    for (int i = 0; i < REG_COUNT; i++){
        be32w(g, vm->reg[i]);
    }

    /*Tabla de segmentos (SEG_COUNT entradas actuales) */
    for (int i = 0; i < SEG_COUNT; i++){
        be16w(g, vm->seg[i].base);
        be16w(g, vm->seg[i].size);
    }

    /*RAM completa del tamaño real */
    size_t bytes_ram = (size_t)vm->ram_kib * 1024u;
    if (fwrite(vm->ram, 1, bytes_ram, g) != bytes_ram){
        fclose(g);
        fprintf(stderr,"VMI: error escribiendo RAM\n");
        return false;
    }

    fclose(g);
    return true;
}
static inline u32 logical_ptr_dyn(int seg_idx, u16 off){
    if (seg_idx < 0)
        return 0xFFFFFFFFu;
    return ((u32)seg_idx << 16) | (u32)off;
}

bool vm_load(VM* vm, char** params, int argc) {
    if (!vm->have_vmx || !vm->opt_vmx_path){
        if (vm->have_vmi && vm->opt_vmi_path){
            return vm_load_vmi(vm, vm->opt_vmi_path);
        } else {
            fprintf(stderr, "No se especificó archivo .vmx ni .vmi.\n");
            return false;
        }
    }

    FILE* f = fopen(vm->opt_vmx_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: No se pudo abrir el archivo %s\n", vm->opt_vmx_path);
        return false;
    }

    u8 hdr6[6];
    if (fread(hdr6,1,6,f) != 6 || memcmp(hdr6,"VMX25",5)!=0){
        fprintf(stderr, "Error: Formato de archivo inválido en %s\n", vm->opt_vmx_path);
        fclose(f);
        return false;
    }
    const int version = hdr6[5];

    u16 code_sz=0, data_sz=0, extra_sz=0, stack_sz=0, const_sz=0, entry_off=0;

    if (version == 1){
        u8 sz2[2];
        if (fread(sz2,1,2,f)!=2){
            fclose(f);
            fprintf(stderr,"Error: encabezado v1 incompleto\n");
            return false;
        }
        code_sz = be16p(sz2);

        const_sz = 0;
        data_sz  = 0;
        extra_sz = 0;
        stack_sz = 0;
        entry_off= 0;
    } else if (version == 2){
        u8 rest[12];
        if (fread(rest,1,12,f)!=12){
            fclose(f);
            fprintf(stderr,"Error: encabezado v2 incompleto\n");
            return false;
        }
        code_sz  = be16p(&rest[0]);
        data_sz  = be16p(&rest[2]);
        extra_sz = be16p(&rest[4]);
        stack_sz = be16p(&rest[6]);
        const_sz = be16p(&rest[8]);
        entry_off= be16p(&rest[10]);
    } else {
        fclose(f);
        fprintf(stderr, "Error: Versión de VMX no soportada (%d)\n", version);
        return false;
    }

    u16 param_sz = 0;
    if (vm->have_params && params && argc > 0){
        u32 need = 0;
        for (int i=0;i<argc;i++){
            const char* s = params[i] ? params[i] : "";
            need += (u32)strlen(s) + 1u;
        }
        need += (u32)argc * 4u; 
        if (need > 0xFFFFu){
            fclose(f);
            fprintf(stderr,"Demasiados parámetros\n");
            return false;
        }
        param_sz = (u16)need;
    }

    const u32 ram_limit = (u32)vm->ram_kib * 1024u;
    u16 cursor = 0;


    if (version == 1){
        const_sz = 0;
        data_sz  = 0;
        extra_sz = 0;
        stack_sz = 0;
        entry_off= 0;
    }


    u16 param_base = 0;
    if (param_sz){
        if (!ensure_ram_capacity(vm, (u16)(cursor + param_sz))){ fclose(f); return false; }
        param_base = place(&cursor, param_sz);
    }

    u16 const_base = 0;
    if (const_sz){
        if (!ensure_ram_capacity(vm, (u16)(cursor + const_sz))){ fclose(f); return false; }
        const_base = place(&cursor, const_sz);
    }

    if (!ensure_ram_capacity(vm, (u16)(cursor + code_sz))){ fclose(f); return false; }
    u16 code_base = place(&cursor, code_sz);

    u16 data_base = 0;
    u16 extra_base = 0;

    if (version == 1){
        u16 remaining = (u16)(ram_limit - cursor);
        data_sz = remaining;
        if (data_sz){
            if (!ensure_ram_capacity(vm, (u16)(cursor + data_sz))){ fclose(f); return false; }
            data_base = place(&cursor, data_sz);
        }
        extra_sz = 0;
        extra_base = 0;
    } else {
        if (data_sz){
            if (!ensure_ram_capacity(vm, (u16)(cursor + data_sz))){ fclose(f); return false; }
            data_base = place(&cursor, data_sz);
        }
        if (extra_sz){
            if (!ensure_ram_capacity(vm, (u16)(cursor + extra_sz))){ fclose(f); return false; }
            extra_base = place(&cursor, extra_sz);
        }
    }

    u16 stack_base = 0;
    if (stack_sz){
        if (!ensure_ram_capacity(vm, (u16)(cursor + stack_sz))){ fclose(f); return false; }
        stack_base = place(&cursor, stack_sz);
    }

    if ((u32)cursor > ram_limit){
        fclose(f);
        fprintf(stderr, "Error: memoria insuficiente para montar el proceso.\n");
        return false;
    }

    if (fread(&vm->ram[code_base], 1, code_sz, f) != code_sz){
        fclose(f);
        fprintf(stderr, "Error: el binario no contiene %u bytes de código\n", code_sz);
        return false;
    }
    if (const_sz){
        if (fread(&vm->ram[const_base], 1, const_sz, f) != const_sz){
            fclose(f);
            fprintf(stderr, "Error: el binario no contiene %u bytes de const\n", const_sz);
            return false;
        }
    }
    fclose(f);

    u16 used_param = 0;
    u16 argv_off   = 0;
    if (param_sz){
        if (!build_param_segment(vm, params, argc,
                                 param_base, param_sz,
                                 &used_param, &argv_off))
        {
            fprintf(stderr, "Error: parámetros exceden el Param Segment\n");
            return false;
        }
    }

    struct TempSeg {
        u16 base;
        u16 size;
        int logical_kind; 
    };

    struct TempSeg tmp[SEG_COUNT];
    int tmp_count = 0;

    #define ADD_SEG(BASE,SIZE,KIND)                        \
        do {                                               \
            if ((SIZE) > 0){                               \
                tmp[tmp_count].base = (BASE);              \
                tmp[tmp_count].size = (SIZE);              \
                tmp[tmp_count].logical_kind = (KIND);      \
                tmp_count++;                               \
            }                                              \
        } while(0)

    ADD_SEG(param_base, param_sz, 0); /* PARAM */
    ADD_SEG(const_base, const_sz, 1); /* CONST */
    ADD_SEG(code_base,  code_sz,  2); /* CODE  */
    ADD_SEG(data_base,  data_sz,  3); /* DATA  */
    ADD_SEG(extra_base, extra_sz, 4); /* EXTRA */
    ADD_SEG(stack_base, stack_sz, 5); /* STACK */

    #undef ADD_SEG

    for (int i=0; i<SEG_COUNT; i++){
        vm->seg[i].base = 0;
        vm->seg[i].size = 0;
    }

    vm->idx_param = -1;
    vm->idx_const = -1;
    vm->idx_code  = -1;
    vm->idx_data  = -1;
    vm->idx_extra = -1;
    vm->idx_stack = -1;

    for (int i=0; i<tmp_count && i<SEG_COUNT; i++){
        vm->seg[i].base = tmp[i].base;
        vm->seg[i].size = tmp[i].size;

        switch (tmp[i].logical_kind){
            case 0: vm->idx_param = i; break;
            case 1: vm->idx_const = i; break;
            case 2: vm->idx_code  = i; break;
            case 3: vm->idx_data  = i; break;
            case 4: vm->idx_extra = i; break;
            case 5: vm->idx_stack = i; break;
        }
    }


    vm->reg[CS] = logical_ptr_dyn(vm->idx_code , 0);
    vm->reg[DS] = logical_ptr_dyn(vm->idx_data , 0);
    vm->reg[ES] = logical_ptr_dyn(vm->idx_extra, 0);
    vm->reg[SS] = logical_ptr_dyn(vm->idx_stack, 0);
    vm->reg[KS] = logical_ptr_dyn(vm->idx_const, 0);
    vm->reg[PS] = logical_ptr_dyn(vm->idx_param, 0);

    {
        u16 entry = (version==2 ? entry_off : 0);
        u16 cs_idx = (u16)(vm->reg[CS] >> 16);
        vm->reg[IP] = ((u32)cs_idx << 16) | (u32)entry;
    }

    if (vm->idx_stack >= 0){
        u16 st_size = vm->seg[vm->idx_stack].size;
        vm->reg[SP] = logical_ptr_dyn(vm->idx_stack, st_size);
        vm->reg[BP] = vm->reg[SP];
    } else {
        vm->reg[SP] = 0xFFFFFFFFu;
        vm->reg[BP] = 0xFFFFFFFFu;
    }


    if (vm->idx_stack >= 0){
        u16 seg_idx = (u16)(vm->reg[SS] >> 16);
        u16 sp      = (u16)(vm->reg[SP] & 0xFFFFu);

        if (sp < 4){ fprintf(stderr,"Error: stack overflow en init\n"); return false; }
        sp -= 4;
        mem_write_u32(vm, seg_idx, sp, 0xFFFFFFFFu);

        if (sp < 4){ fprintf(stderr,"Error: stack overflow en init\n"); return false; }
        sp -= 4;
        mem_write_u32(vm, seg_idx, sp, (u32)argc);

        u32 argv_ptr = 0xFFFFFFFFu;
        if (vm->idx_param >= 0 && argc > 0){
            argv_ptr = ((u32)vm->idx_param << 16) | (u32)argv_off;
        }

        if (sp < 4){ fprintf(stderr,"Error: stack overflow en init\n"); return false; }
        sp -= 4;
        mem_write_u32(vm, seg_idx, sp, argv_ptr);

        vm->reg[SP] = ((u32)seg_idx << 16) | (u32)sp;
    }

    vm->reg[OPC] = 0;
    vm->reg[OP1] = 0;
    vm->reg[OP2] = 0;
    vm->reg[CC]  = 0;

    if (vm->idx_code >= 0){
        vm->code_size = (u16)(vm->seg[vm->idx_code].base + vm->seg[vm->idx_code].size);
    } else {
        vm->code_size = 0;
    }

    return true;
}


int vm_run(VM* vm) {
    OpHandler table[256];
    init_dispatch_table(table);

    if (vm->disassemble) {
        disasm_dump_segments(vm);
        disasm_dump_const_strings(vm);
    }

    for (;;) {

        if (vm->reg[IP] == 0xFFFFFFFFu) {
            return 0;
        }

        u16 seg = (u16)(vm->reg[IP] >> 16);
        u16 off = (u16)(vm->reg[IP] & 0xFFFFu);

        if (off == vm->seg[seg].size) {
            return 0;
        }

        if (off > vm->seg[seg].size) {
            fprintf(stderr, "Error: instruccion invalida\n");
            return 1;
        }

        u16 phys;
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
            return 1;
        }
    }
}

