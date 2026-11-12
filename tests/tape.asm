; test cassetta: scrive 1000 byte con INT 15h AH=03, riavvolge (via menu), rilegge e confronta
org 0x100
  mov ah,0          ; motore on
  int 0x15
  ; prepara dati
  mov di,buf
  mov cx,1000
  xor al,al
.f: stosb
  add al,7
  loop .f
  mov si,arg
  mov al,[0x81+1]
  cmp al,'R'
  je read
  mov ah,3
  mov bx,buf
  mov cx,1000
  int 0x15
  mov dx,okw
  jnc .p
  mov dx,errw
.p: mov ah,9
  int 0x21
  ret
read:
  push cs
  pop es
  mov ah,2
  mov bx,buf2
  mov cx,1000
  int 0x15
  jc .bad
  cmp dx,1000
  jne .bad
  mov si,buf
  mov di,buf2
  mov cx,1000
  repe cmpsb
  mov dx,errc
  jne .p
  mov dx,okr
  jmp .p
.bad: mov dx,errr
.p: mov ah,9
  int 0x21
  ret
arg:
okw: db 'SCRITTURA OK$'
errw: db 'ERRORE SCRITTURA$'
okr: db 'LETTURA OK, DATI IDENTICI$'
errr: db 'ERRORE LETTURA$'
errc: db 'DATI DIVERSI$'
buf: times 1000 db 0
buf2: times 1000 db 0
