; test grafica CGA 320x200 + testo in grafica + beep
org 0x100
  mov ax,0x0004
  int 0x10
  mov ah,0x0B
  mov bx,0x0101       ; palette 1
  int 0x10
  ; cornice e diagonali con INT 10h AH=0Ch
  xor cx,cx
.l1: mov dx,0
  mov ax,0x0C03
  int 0x10
  mov dx,199
  int 0x10
  inc cx
  cmp cx,320
  jne .l1
  xor dx,dx
.l2: mov cx,0
  mov ax,0x0C02
  int 0x10
  mov cx,319
  int 0x10
  ; diagonale
  mov cx,dx
  add cx,60
  mov ax,0x0C01
  int 0x10
  inc dx
  cmp dx,200
  jne .l2
  ; barre colorate scritte direttamente in VRAM
  mov ax,0xB800
  mov es,ax
  mov di,80*40+10
  mov bx,40
.bars:
  mov al,0x55
  mov cx,15
  rep stosb
  mov al,0xAA
  mov cx,15
  rep stosb
  mov al,0xFF
  mov cx,15
  rep stosb
  add di,80-45
  cmp di,0x2000
  jb .ok
  sub di,0x2000-80+80
.ok:
  dec bx
  jnz .bars
  ; testo
  mov ah,2
  xor bh,bh
  mov dx,0x1205
  int 0x10
  mov si,msg
.t: lodsb
  or al,al
  jz .done
  mov ah,0x0E
  mov bl,3
  int 0x10
  jmp .t
.done:
  ; beep: canale 2 a ~1 kHz
  mov al,0xB6
  out 0x43,al
  mov ax,1193
  out 0x42,al
  mov al,ah
  out 0x42,al
  in al,0x61
  or al,3
  out 0x61,al
  mov cx,0xFFFF
.w: loop .w
  in al,0x61
  and al,0xFC
  out 0x61,al
  ; scrive un file su disco
  mov ah,0x3C
  xor cx,cx
  mov dx,fname
  int 0x21
  mov bx,ax
  mov ah,0x40
  mov cx,msglen
  mov dx,msg
  int 0x21
  mov ah,0x3E
  int 0x21
  xor ah,ah
  int 0x16
  mov ax,3
  int 0x10
  ret
msg: db 'Prodest PC1 - CGA 320x200',0
msglen equ $-msg-1
fname: db 'PROVA.TXT',0
