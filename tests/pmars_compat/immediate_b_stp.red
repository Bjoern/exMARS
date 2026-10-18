;redcode-94
;name immediate B after A postincrement
;assert CORESIZE == 8000
; A points at cell 6, caches 7, then increments this instruction's B to 7.
; B is immediate, so its evaluated value is now 7: store in P-space[7].
stp.ab >0, #6
ldp.ab #7, value
seq.ab #7, value
dat 0,0
jmp 0
value dat 0,0
dat 7,0
end
