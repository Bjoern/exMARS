;redcode-94
;name wide fields
;assert CORESIZE == 55440
; 30000+40000 and 10000-40000 leave 16 bits before reduction mod 55440.
        add.ab #40000, t1
        seq.ab #14560, t1
        dat    0, 0
        sub.ab #40000, t2
        seq.ab #25440, t2
        dat    0, 0
        jmp    0
t1      dat    0, 30000
t2      dat    0, 10000
end
