.set noreorder                 
            li     $s0, 40 
loop_start: slti   $t0, $s1, 5
            beq    $t0, $zero, loop_end # if s1 = 5 then branch
            sll    $t1, $s1, 2
            lw     $t2, 44($t1)
            addi   $s0, $s0, 1 
            add    $t3, $t2, $s0 
            sw     $t3, 44($t1)
            j      loop_start
            addi   $s1, $s1, 1
loop_end:   .word  0xfeedfeed
            .word  0
            .word  195 
            .word  -554
            .word  467545
            .word  -1
            