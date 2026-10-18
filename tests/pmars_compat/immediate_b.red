;redcode-94
;name immediate B after A side effect
;assert 1
; A (>0) points at the DAT below and increments this instruction's B-field
; from 2 to 3. pMARS reads the immediate B operand after that, so B = 3 + 0.
start   add.b  >0, #2
        seq.ab #3, start
        dat    0, 0
        jmp    0
end
