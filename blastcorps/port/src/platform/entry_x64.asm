; MSVC x64: function-entry hooks for the game code (the counterpart of gcc's
; -finstrument-functions / __cyg_profile_func_enter: --sync's function-entry
; switch points and --calls, os_thread.c).
;
; plat_host.c host_entry_hook patches the functions to hook, and only those:
; the function's first instruction becomes a short jump into the padding the
; linker leaves before it (/FUNCTIONPADMIN; MSVC's x64 code starts every
; function with an instruction of at least 2 bytes), which jumps to a thunk of
; its own:
;       push  FUNCTION            ; the key
;       call  port_entry_common   ; returns to the next line, dropping the key
;       <the function's first instruction, relocated>
;       jmp   FUNCTION + its length
; so the hook runs before any instruction of the function (MSVC's /Gh hook,
; _penter, is called after the prologue, and the compiler schedules body
; instructions, N64 memory accesses among them, before it).
;
; port_entry_common saves every volatile register, aligns the stack and calls
;   port_entry_hook(fn, rsp, saved)
;     fn     the function's address
;     rsp    its rsp at entry (-> its return address)
;     saved  -> its r9, r8, rdx, rcx (in that order)
; which may switch fibers (a --sync switch point) before it returns.
;
; _pexit: the game files are built /GH, so every function calls _pexit before
; it returns.  It does nothing, but its presence keeps the compiler from
; turning a call in tail position into a jump: return addresses name their
; real callers (gcc's -fno-optimize-sibling-calls), which --clock and --sync
; key on.

EXTERN port_entry_hook:PROC

_TEXT SEGMENT

port_entry_common PROC FRAME
    push    rbp
    .pushreg rbp
    mov     rbp, rsp
    .setframe rbp, 0
    .endprolog
    ; [rbp+8] = the thunk's resume address, [rbp+16] = the key (the function),
    ; [rbp+24] = the function's return address
    push    rax                 ; [rbp-8]
    push    rcx                 ; [rbp-16]
    push    rdx                 ; [rbp-24]
    push    r8                  ; [rbp-32]
    push    r9                  ; [rbp-40]
    push    r10                 ; [rbp-48]
    push    r11                 ; [rbp-56]
    and     rsp, -16
    sub     rsp, 80h            ; home area + xmm0-5
    movdqa  xmmword ptr [rsp+20h], xmm0
    movdqa  xmmword ptr [rsp+30h], xmm1
    movdqa  xmmword ptr [rsp+40h], xmm2
    movdqa  xmmword ptr [rsp+50h], xmm3
    movdqa  xmmword ptr [rsp+60h], xmm4
    movdqa  xmmword ptr [rsp+70h], xmm5
    mov     rcx, [rbp+16]       ; the function
    lea     rdx, [rbp+24]       ; its rsp at entry
    lea     r8, [rbp-40]        ; saved r9, r8, rdx, rcx
    call    port_entry_hook
    movdqa  xmm0, xmmword ptr [rsp+20h]
    movdqa  xmm1, xmmword ptr [rsp+30h]
    movdqa  xmm2, xmmword ptr [rsp+40h]
    movdqa  xmm3, xmmword ptr [rsp+50h]
    movdqa  xmm4, xmmword ptr [rsp+60h]
    movdqa  xmm5, xmmword ptr [rsp+70h]
    lea     rsp, [rbp-56]
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdx
    pop     rcx
    pop     rax
    pop     rbp
    ret     8
port_entry_common ENDP

_pexit PROC
    ret
_pexit ENDP

_TEXT ENDS
END
