#include "vm.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv){
  if (argc < 2) {
    fprintf(stderr, "Uso: %s [-d] programa.vmx\n", argv[0]);
    return 1;
  }

  int argi = 1;
  int dis = 0;
  if (strcmp(argv[argi], "-d") == 0) { dis = 1; argi++; }
  if (argi >= argc) {
    fprintf(stderr, "Falta el archivo .vmx\n");
    return 1;
  }

  VM vm;
  vm_init(&vm, dis);
  if (!vm_load(&vm, argv[argi])) return 1;
  return vm_run(&vm);
}