main:   addi    $t0, $zero, -32         # $t0 =   0xFFFFFFE0
        addi    $t1, $zero, -500        # $t1 =   0xFFFFFE0C
        li      $t2, 0xABCDABCD
        addi    $t3, $zero, 4           # $t3 =   0x00000004
        sb      $t2, 60($zero)          # M[60] = 0x000000AB
        sb      $t1, 64($zero)          # M[64] = 0x000000FF
        sh      $t2, 68($zero)          # M[68] = 0x0000ABCD
        sh      $t1, 72($zero)          # M[72] = 0x0000FFFF
        sw      $t2, 76($zero)          # M[76] = 0xABCD0000
        sw      $t1, 76($t3)            # M[80] = 0xFFFFFE0C
        lw	$t4, 80($zero)		# t4 = 0xFFFFFE0C
        sw      $t4, 80($t3)            # M[84] = 0xFFFFFE0C

        .word   0xfeedfeed              # M = 48
