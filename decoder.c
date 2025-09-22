#include "decoder.h"
#include "memory.h"
#include "vm.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Mapea código binario de tipo (00/01/10/11) a nuestro enum
static inline OperandType type_from_code(uint8_t code){
    switch (code & 0x3){
        case 0: return OT_NONE;  // 00
        case 1: return OT_REG;   // 01
        case 2: return OT_IMM;   // 10
        case 3: return OT_MEM;   // 11
    }
    return OT_NONE;
}



// Utilidad: leer N bytes desde código (no toca LAR/MAR/MBR) con chequeo de segmento
static bool fetch_bytes(VM* vm, uint16_t seg, uint16_t off, uint16_t n, uint16_t* phys, uint8_t* dst){
    if (!translate_and_check(vm, seg, off, n, phys)) return false;
    code_read_bytes(vm, *phys, dst, n);  // acceso a "código"
    return true;
}

// Clasificación por cantidad de operandos (tabla del TP)
static inline bool is_two_ops(uint8_t opc){ return (opc >= 0x10 && opc <= 0x1F); } // MOV..RND :contentReference[oaicite:8]{index=8}
static inline bool is_one_op (uint8_t opc){ return (opc <= 0x08); }                // SYS..NOT   :contentReference[oaicite:9]{index=9}
static inline bool is_zero_op(uint8_t opc){ return (opc == 0x0F); }               // STOP       :contentReference[oaicite:10]{index=10}

// Empaqueta OPx: [31..24]=tipo(00/01/10/11) ; [23..0]=hasta 3 bytes crudos del operando
static inline uint32_t pack_op(OperandType t, const uint8_t* raw, uint8_t n){
    uint32_t v = ((uint32_t)(t & 0x3)) << 24;
    // Copiamos "tal cual está codificado" en memoria (orden natural de bytes)
    // Primer byte del operando -> bits [23..16], etc. :contentReference[oaicite:11]{index=11}
    if (n > 3) n = 3;
    for (uint8_t i = 0; i < n; ++i){
        v |= ((uint32_t)raw[i]) << (8 * (2 - i));
    }
    return v;
}


bool fetch_and_decode(VM* vm, DecodedInst* di){
    // ---- 1) Donde estamos: IP lógico -> físico del primer byte (header) ----
    uint16_t seg = (uint16_t)(vm->reg[IP] >> 16);
    uint16_t off = (uint16_t)(vm->reg[IP] & 0xFFFFu);

    uint16_t phys0 = 0;
    uint8_t  hdr   = 0;
    if (!fetch_bytes(vm, seg, off, 1, &phys0, &hdr)) return false;

    // ---- 2) Inicialización de la estructura DecodedInstr ----
    memset(di, 0, sizeof(*di));
    di->phys   = phys0;
    di->size   = 1;
    di->opcode = (uint8_t)(hdr & 0x1F);      // 5 bits bajos = opcode :contentReference[oaicite:12]{index=12}
    di->A.type = OT_NONE;
    di->B.type = OT_NONE;

    // Guardar OPC/OP1/OP2 según TP
    vm->reg[OPC] = (uint32_t)di->opcode;     // "almacenar el código de operación en OPC" :contentReference[oaicite:13]{index=13}
    vm->reg[OP1] = 0;
    vm->reg[OP2] = 0;

    // ---- 3) Determinar tipos desde el header (según cantidad de operandos) ----
    if (is_two_ops(di->opcode)){
        // B: bits 7-6 ; A: bit 5 (0=REG, 1=MEM). Operandos vienen B luego A. :contentReference[oaicite:14]{index=14} :contentReference[oaicite:15]{index=15}
        OperandType typeB = type_from_code((uint8_t)(hdr >> 6));                 // 00/01/10/11
        OperandType typeA = ((hdr >> 5) & 0x1) ? OT_MEM : OT_REG;                // A no puede ser IMM en 2-op

        // Tamaños
        uint8_t nB = size_from_type(typeB);
        uint8_t nA = size_from_type(typeA);

        // Leer B (bytes crudos)
        if (nB){
            uint16_t physB = 0;
            if (!fetch_bytes(vm, seg, (uint16_t)(off + di->size), nB, &physB, di->B.raw)) return false;
            di->B.type = typeB;
            di->B.size = nB;
            di->size  += nB;
        }

        // Leer A (bytes crudos)
        if (nA){
            uint16_t physA = 0;
            if (!fetch_bytes(vm, seg, (uint16_t)(off + di->size), nA, &physA, di->A.raw)) return false;
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }

    } else if (is_one_op(di->opcode)){
        // A: bits 7-6 = tipo (00/01/10/11). :contentReference[oaicite:16]{index=16} :contentReference[oaicite:17]{index=17}
        OperandType typeA = type_from_code((uint8_t)(hdr >> 6));
        uint8_t nA   = size_from_type(typeA);

        if (nA){
            uint16_t physA = 0;
            if (!fetch_bytes(vm, seg, (uint16_t)(off + di->size), nA, &physA, di->A.raw)) return false;
            di->A.type = typeA;
            di->A.size = nA;
            di->size  += nA;
        }
    } else if (is_zero_op(di->opcode)){
        // STOP: sin operandos (nada que leer). :contentReference[oaicite:18]{index=18}
        // di->size ya es 1
    } else {
        // opcode inválido (no listado)
        return false;
    }

    // ---- 4) Guardar OP1/OP2 con el formato pedido ----
    vm->reg[OP1] = pack_op(di->A.type, di->A.raw, di->A.size);  // [31..24]=tipo ; [23..0]=bytes crudos  :contentReference[oaicite:19]{index=19}
    vm->reg[OP2] = pack_op(di->B.type, di->B.raw, di->B.size);

    // ---- 5) Avanzar IP a la próxima instrucción ----
    uint16_t new_off = (uint16_t)(off + di->size);
    vm->reg[IP] = ((uint32_t)seg << 16) | new_off;              // sumar tamaño actual :contentReference[oaicite:20]{index=20}

    return true;
}

    
