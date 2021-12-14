# A simple test case that includes a load-use stall and a branch.
.set noreorder
main:   addi    $t0, $zero, 4
        addi    $t1, $zero, 52
        lw      $s1, 32($zero)     # s1 = AA
        lw      $s1, 16($zero)     # s2 = AAAA
        lw      $s1, 32($zero)     # s1 = AA
        lw      $s1, 16($zero)     # s2 = AAAA
        lw      $s1, 32($zero)     # s1 = AA
        lw      $s1, 16($zero)     # s2 = AAAA
        .word   0xfeedfeed  # pos 40
        .word   0x0         # pos 44
        .word   0x0         # pos 48
        .word   0xAAAAAAAA  # pos 52
        .word   0xF6F7F8F9  # pos 56
        