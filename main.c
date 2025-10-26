#include "vm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char** argv){
  if (argc < 2){
    fprintf(stderr, "Uso:\n  %s [programa.vmx] [imagen.vmi] m=M [-d] [-p param1 ... paramN]\n", argv[0]);
    return 1;
  }

  VM vm; vm_init(&vm, 0);
  vm.ram_kib = RAM_DEFAULT_KIB;

  int i=1;
  for(; i<argc; ++i){
    if (strcmp(argv[i], "-d")==0){ vm.disassemble=1; continue; }
    if (argv[i][0]=='m' && argv[i][1]=='=' ){
      vm.ram_kib = (uint32_t)strtoul(argv[i]+2, NULL, 10);
      if (vm.ram_kib==0){ fprintf(stderr,"m debe ser >0\n"); return 1; }
      continue;
    }
    if (strcmp(argv[i], "-p")==0){
      vm.have_params = 1;
      i++; break;
    }
    if (strstr(argv[i], ".vmx")){ vm.opt_vmx_path=argv[i]; vm.have_vmx=1; continue; }
    if (strstr(argv[i], ".vmi")){ vm.opt_vmi_path=argv[i]; vm.have_vmi=1; continue; }
  }

  if (!vm.have_vmx && !vm.have_vmi){
    fprintf(stderr, "Debe especificar .vmx y/o .vmi.\n");
    return 1;
  }

  vm.argc_on_stack = 0;
  char** params = NULL;
  if (vm.have_vmx && vm.have_params){
    params = &argv[i];
    vm.argc_on_stack = argc - i;
  }

  if (!vm_load(&vm, params, vm.argc_on_stack)) return 1;
  return vm_run(&vm);
}
