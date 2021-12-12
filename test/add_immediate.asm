      addi   $s0, $zero, 4
      addiu  $s2, $zero, 300
      addiu  $s3, $zero, -500
      addi   $s4, $s3, 300 
      sltiu  $t0, $s2, 200    # t0 = 0
      sltiu  $t1, $s2, 400    # t1 = 1
      sltiu  $t2, $s2, -200   # t2 = 1
.word 0xfeedfeed
