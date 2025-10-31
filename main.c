#include "vm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

int main(int argc, char** argv){
  if (argc < 2){
    fprintf(stderr,"Uso:\n" "  %s programa.vmx [param1 param2 ...]\n" "  %s programa.vmx [-d] [m=KIB] [-p param1 ...]\n" "  %s imagen.vmi [-d]\n", argv[0], argv[0], argv[0]);
    return 1;
  }

  VM vm;
  vm_init(&vm, /*disassemble=*/0);
  vm.ram_kib = RAM_DEFAULT_KIB;

  int user_args_start = -1;
  bool saw_p_flag = false;

  for (int i = 1; i < argc; ++i){
    const char* a = argv[i];

    if (strcmp(a, "-d") == 0){
      vm.disassemble = 1;
      continue;
    }

    if (a[0]=='m' && a[1]=='='){
      vm.ram_kib = (uint32_t)strtoul(a+2, NULL, 10);
      if (vm.ram_kib == 0){
        fprintf(stderr,"m debe ser >0\n");
        return 1;
      }
      continue;
    }

    if (strcmp(a, "-p") == 0){
      saw_p_flag = true;
      if (i+1 < argc){
        user_args_start = i+1;
      } else {
        user_args_start = -1; // no hay args después de -p
      }
      break;
    }

    if (strstr(a, ".vmi")){
      vm.opt_vmi_path = argv[i];
      vm.have_vmi = 1;
      continue;
    }

    if (strstr(a, ".vmx")){
      vm.opt_vmx_path = argv[i];
      vm.have_vmx = 1;
      continue;
    }

    if (!saw_p_flag && vm.have_vmx && user_args_start == -1){
      user_args_start = i;

    }
  }

  if (!vm.have_vmx && !vm.have_vmi){
    fprintf(stderr, "Debe especificar .vmx o .vmi.\n");
    return 1;
  }

  char** params = NULL;
  int param_count = 0;

  if (vm.have_vmx){
    if (user_args_start >= 0 && user_args_start < argc){
      params = &argv[user_args_start];
      param_count = argc - user_args_start;
    } else {
      params = NULL;
      param_count = 0;
    }


    if (param_count > 0){
      vm.have_params = 1;
    } else {
      vm.have_params = 0;
    }

    vm.argc_on_stack = param_count;
  } else {
    vm.have_params = 0;
    vm.argc_on_stack = 0;
    params = NULL;
    param_count = 0;
  }
  if (!vm_load(&vm, params, param_count)){
    fprintf(stderr,"No pude cargar la VM.\n");
    return 1;
  }

  int rc = vm_run(&vm);
  return rc;
}
