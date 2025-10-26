MAIN:
    MOV EAX, 0
    MOV EBX, 0
    MOV ECX, 0
    MOV EDX, 0
    MOV AL, 0xF0
    PUSH AL
    POP  CL
    PUSH 0x7FFF
    MOV  EAX, 0
    POP  AX
    MOV [DS+0],  EAX
    MOV [DS+4],  EBX
    MOV [DS+8],  ECX
    MOV EAX, 0
    LDH EAX, 0xFFFF
    LDL EAX, 0xFFFF
    PUSH EAX
    RET
    SYS 2
    STOP
