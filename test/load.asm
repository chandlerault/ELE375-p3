main:   addi    $t0, $zero, 4
        addi    $t1, $zero, 52
        lbu     $s1, 0($t1)     # s1 = AA
        lhu     $s2, 0($t1)     # s2 = AAAA
        lui     $s3, 0x1001     # S3 = 0x1001000  
        lw      $s4, 48($t0)    # s4 = 0xAAAA
        lbu     $s5, 52($t0)    # s5 = 0xF6
        lhu     $s6, 52($t0)    # s6 = 0x0000FFF6
        lw      $s7, 52($t0)    # s7 = 0xFFFFFFF6
        .word   0xfeedfeed  # pos 40
        .word   0x0         # pos 44
        .word   0x0         # pos 48
        .word   0xAAAAAAAA  # pos 52
        .word   0xF6F7F8F9  # pos 56
        