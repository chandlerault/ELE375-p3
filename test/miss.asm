# A simple test case to ensure that cache misses in direct mapped and hits in 2-way associative
.set noreorder
main:   addi    $t0, $zero, 4
        addi    $t1, $zero, 52
        lw      $s1, 0($zero)     # s1 = AA
        lw      $s2, 1024($zero)     # s2 = AAAA
        lw      $s1, 0($zero)     # s1 = AA
        lw      $s2, 1024($zero)     # s2 = AAAA
        lw      $s1, 0($zero)     # s1 = AA
        lw      $s2, 1024($zero)     # s2 = AAAA
        .word   0xfeedfeed  # pos 40
        .word   0x0         # pos 44
        .word   0x0         # pos 48
        .word   0xAAAAAAAA  # pos 52
        .word   0xF6F7F8F9  # pos 56
        