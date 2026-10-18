;redcode-94
;name seed zero
;assert CORESIZE == 8000
; Kills a one-cell warrior at 0 only when loaded at the minimum separation
; (100). With -F 100 the placement seed is 0: pMARS advances it to other
; positions in later rounds, the old exMARS generator stayed at 0 forever.
        mov.i  bomb, -100
        jmp    0
bomb    dat    0, 0
end
