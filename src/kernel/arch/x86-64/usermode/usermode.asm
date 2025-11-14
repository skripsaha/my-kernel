[BITS 64]

; ============================================================================
; USER MODE ASSEMBLY - Ring 3 Transitions
; ============================================================================

section .text

global usermode_jump_ring3
global usermode_return_ring0

; ============================================================================
; usermode_jump_ring3(uint64_t rip, uint64_t rsp, uint64_t rflags)
; ============================================================================
; Switches from Ring 0 (kernel) to Ring 3 (user mode)
; Uses IRET to change privilege level
;
; Stack frame for IRET (must be set up):
;   [SS]      User data segment (0x33 = 0x30 | RPL 3)
;   [RSP]     User stack pointer
;   [RFLAGS]  User flags (IF=1)
;   [CS]      User code segment (0x2B = 0x28 | RPL 3)
;   [RIP]     User instruction pointer
;
; Parameters:
;   RDI = User RIP (entry point)
;   RSI = User RSP (stack pointer)
;   RDX = User RFLAGS
; ============================================================================

usermode_jump_ring3:
    cli                      ; Disable interrupts during setup

    ; Save parameters to stack
    push rdx                 ; RFLAGS
    push rsi                 ; RSP
    push rdi                 ; RIP

    ; Set up IRET stack frame
    ; We need to push: SS, RSP, RFLAGS, CS, RIP

    ; FIXED: User data segment (0x20 + RPL 3 = 0x23)
    ; GDT_USER_DATA (0x20) with RPL=3
    mov ax, 0x23
    push rax                 ; Push SS (user data segment)

    mov rax, [rsp + 8]       ; Get RSP from saved params
    push rax                 ; Push RSP (user stack)

    mov rax, [rsp + 16]      ; Get RFLAGS from saved params
    or rax, 0x200            ; Ensure IF (interrupts enabled)
    push rax                 ; Push RFLAGS

    ; FIXED: User code segment (0x18 + RPL 3 = 0x1B)
    ; GDT_USER_CODE (0x18) with RPL=3
    mov ax, 0x1B
    push rax                 ; Push CS (user code segment)

    mov rax, [rsp + 24]      ; Get RIP from saved params
    push rax                 ; Push RIP (user entry point)

    ; Clean up saved parameters from stack
    add rsp, 24              ; Remove saved params

    ; Set user data segments
    mov ax, 0x23             ; FIXED: User data segment (0x20 | 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Jump to Ring 3!
    ; IRET will pop: RIP, CS, RFLAGS, RSP, SS
    ; and set CPL = RPL of CS (which is 3)
    iretq

; ============================================================================
; usermode_return_ring0()
; ============================================================================
; Returns from Ring 3 to Ring 0
; This is called by interrupt/exception handlers
; ============================================================================

usermode_return_ring0:
    ; When an interrupt occurs in Ring 3, CPU automatically:
    ; 1. Switches to Ring 0
    ; 2. Loads kernel stack from TSS
    ; 3. Pushes: SS, RSP, RFLAGS, CS, RIP
    ; 4. Jumps to interrupt handler

    ; Restore kernel data segments
    mov ax, 0x20             ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Return to caller (interrupt handler)
    ret

; ============================================================================
; Helper: Get current privilege level
; ============================================================================

global usermode_get_cpl

usermode_get_cpl:
    mov rax, cs              ; Get CS register
    and rax, 3               ; Extract RPL (bits 0-1)
    ret                      ; Return CPL in RAX
