.set noreorder                 
addi    $s0, $zero, -31    # s0 = -31
add     $zero, $zero, $s0 
or      $s1, $zero, $s0    # s1 = -31 
addi    $s2, $s0, 40       # s2 = 9 
and     $s3, $s0, $s2      # s3 = 1 
nor     $s4, $zero, $s3    # s4 = -2 
or      $s5, $zero, $s3    # s5 = 1 
slt     $t0, $s4, $zero    # t0 = 1
sltu    $t1, $s4, $t8      # t1 = 0
slt     $t2, $s2, $s2      # t2 = 0
sll     $t3, $s2, 3        # t3 = 72 
or      $t4, $t3, $s2      # t4 = 73
srl     $t5, $t4, 3        # t5 = 9
# TODO: test shifts with weird shamt?
sub     $t6, $t3, $s1      # t6 = 103
subu    $t7, $s4, $s4      # t7 = 0
li      $ra, 72
jr      $ra 
add     $t8, $t8, $s3      
add     $t8, $t8, $s3      # t8 = 2
.word   0xfeedfeed
