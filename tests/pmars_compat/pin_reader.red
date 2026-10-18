;redcode-94
;name pin_reader
;assert CORESIZE == 8000
pin 123
nop 0,0
ldp.ab #1,value
seq.ab #7,value
dat 0,0
jmp 0
value dat 0,0
end
