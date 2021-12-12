.set noreorder 
main:       addi   $s0, $zero, 4
            andi   $s0, $s0, 4
            andi   $s1, $s0, 300
loop:       addi   $t0, $t0, 1
            bne    $t0, $s1, loop
            nop
            addi   $t0, $zero, 20
            addi   $t1, $zero, 5 
loop2:      addi   $t1, $t1, -1
            beq    $t1, 4, loop2
            slti    $t2, $t1, 6          # t2 = 1 
            slti    $t3, $t1, -5         # t3 = 0 
.word 0xfeedfeed
