; Prodest PC1 Emulator - BIOS integrato (parte in ROM)
; Le routine vere e proprie sono implementate nell'emulatore (hlebios.cpp):
; qui ci sono solo i punti di ingresso agli indirizzi standard IBM PC/XT,
; che richiamano l'emulatore con l'istruzione "trap" F1 nn.
; Assemblare con:  nasm -f bin -o biosrom.bin biosrom.asm
; Il binario copre F000:E000 - F000:FFFF.

        cpu     8086
        org     0xE000

%macro AT 1
        times (%1 - 0xE000) - ($ - $$) db 0xFF
%endmacro
%macro TRAP 1
        db      0xF1, %1
%endmacro

rom_start:
        db      'Prodest PC1 Emulator - BIOS integrato (C) 2026 - GPLv3', 0

; ---------------------------------------------------------------- POST
        AT      0xE05B
post:
        cli
        xor     ax, ax
        mov     ss, ax
        mov     sp, 0x0400
        TRAP    0x00                    ; inizializza tutto (memoria, IVT, BDA, PIC, PIT, video)
        sti
        int     0x19

; ---------------------------------------------------------------- NMI
        AT      0xE2C3
nmi:    iret

; ---------------------------------------------------------------- INT 18h - ROM BASIC assente
        AT      0xE3A0
int18:
        sti
        TRAP    0x18
        xor     ah, ah
        int     0x16
        int     0x19

; ---------------------------------------------------------------- INT 19h - bootstrap
        AT      0xE6F2
int19:
        sti
.retry:
        TRAP    0x19                    ; CF=0: settore di avvio caricato a 0000:7C00
        jnc     .boot
        xor     ah, ah
        int     0x16                    ; attende un tasto e riprova
        jmp     .retry
.boot:
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        xor     dx, dx                  ; DL = unita' di avvio (A:)
        jmp     0x0000:0x7C00

; ---------------------------------------------------------------- INT 14h - seriale (assente)
        AT      0xE739
int14:
        sti
        TRAP    0x14
        iret

; ---------------------------------------------------------------- INT 16h - tastiera
        AT      0xE82E
int16:
        sti
.again:
        TRAP    0x16                    ; CF=1 -> buffer vuoto, attendere
        jnc     .done
        hlt
        jmp     .again
.done:
        retf    2

; ---------------------------------------------------------------- INT 09h - IRQ1 tastiera
        AT      0xE987
int09:
        push    ax
        TRAP    0x09                    ; AL bit0 = Ctrl+Break premuto
        test    al, 1
        jz      .out
        int     0x1B
.out:
        pop     ax
        iret

; ---------------------------------------------------------------- INT 13h - dischi
        AT      0xEC59
int13:
        sti
        TRAP    0x13
        retf    2

; ---------------------------------------------------------------- INT 0Eh - IRQ6 floppy
        AT      0xEF57
int0e:
        push    ax
        TRAP    0x0E
        pop     ax
        iret

; ---------------------------------------------------------------- tabella parametri floppy
        AT      0xEFC7
diskette_param:
        db      0xDF, 0x02, 0x25, 0x02, 0x09, 0x2A, 0xFF, 0x50, 0xF6, 0x0F, 0x02

; ---------------------------------------------------------------- INT 17h - stampante (assente)
        AT      0xEFD2
int17:
        sti
        TRAP    0x17
        iret

; ---------------------------------------------------------------- INT 10h - video
        AT      0xF065
int10:
        sti
        TRAP    0x10
        iret

; ---------------------------------------------------------------- tabelle video (INT 1Dh)
        AT      0xF0A4
video_parms:
        db      0x38, 0x28, 0x2D, 0x0A, 0x1F, 0x06, 0x19, 0x1C, 0x02, 0x07, 0x06, 0x07, 0, 0, 0, 0
        db      0x71, 0x50, 0x5A, 0x0A, 0x1F, 0x06, 0x19, 0x1C, 0x02, 0x07, 0x06, 0x07, 0, 0, 0, 0
        db      0x38, 0x28, 0x2D, 0x0A, 0x7F, 0x06, 0x64, 0x70, 0x02, 0x01, 0x06, 0x07, 0, 0, 0, 0
        db      0x61, 0x50, 0x52, 0x0F, 0x19, 0x06, 0x19, 0x19, 0x02, 0x0D, 0x0B, 0x0C, 0, 0, 0, 0
        dw      0x0800, 0x1000, 0x4000, 0x4000
        db      0x28, 0x28, 0x50, 0x50, 0x28, 0x28, 0x50, 0x50
        db      0x2C, 0x28, 0x2D, 0x29, 0x2A, 0x2E, 0x1E, 0x29

; ---------------------------------------------------------------- INT 12h / 11h / 15h
        AT      0xF841
int12:
        TRAP    0x12
        iret

        AT      0xF84D
int11:
        TRAP    0x11
        iret

        AT      0xF859
int15:
        sti
        TRAP    0x15
        retf    2

; ---------------------------------------------------------------- font 8x8 (caricato dall'emulatore)
        AT      0xFA6E
        times   1024 db 0

; ---------------------------------------------------------------- INT 1Ah - orologio
        AT      0xFE6E
int1a:
        sti
        TRAP    0x1A
        retf    2

; ---------------------------------------------------------------- INT 08h - IRQ0 timer
        AT      0xFEA5
int08:
        push    ax
        push    ds
        TRAP    0x08                    ; conteggio tick, motore floppy
        int     0x1C
        cli
        mov     al, 0x20
        out     0x20, al
        pop     ds
        pop     ax
        iret

; ---------------------------------------------------------------- IRQ generico
        AT      0xFF23
irq_default:
        push    ax
        mov     al, 0x20
        out     0x20, al
        pop     ax
        iret

        AT      0xFF53
dummy_iret:
        iret

; ---------------------------------------------------------------- INT 05h - stampa schermo
        AT      0xFF54
int05:
        iret

; ---------------------------------------------------------------- reset
        AT      0xFFF0
        jmp     0xF000:post
        db      '11/11/26'
        db      0xFF
        db      0xFE                    ; modello: PC/XT
        db      0xFF
