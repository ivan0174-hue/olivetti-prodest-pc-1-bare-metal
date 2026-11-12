org 0x100
.l: mov ax,[si]
  add bx,ax
  inc si
  and si,0x3ff
  mov [di],bx
  xor dx,ax
  loop .l
  jmp .l
