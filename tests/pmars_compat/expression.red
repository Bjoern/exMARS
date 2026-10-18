;redcode-94
;name expression chains
;assert 1
; Each JMP must evaluate to 1. The old evaluator grouped a-(b)/2-c/2 as
; a-((b)/2-c/2), assembling Bipolar's and Twister's offsets differently.
        jmp    9-(3*4)/2-4/2
        jmp    -62-(3*48)--6*(8*3/2-1)-60+201
        jmp    10-2-3-4
        jmp    2*3%4-1
        jmp    0
end
