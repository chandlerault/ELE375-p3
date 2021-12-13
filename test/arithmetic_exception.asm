li     $t0, 0xffffffff
li     $t1, 2
addu   $t2, $t0, $t1 
subu   $t3, $t1, $t0 
addiu  $t4, $t0, 2 
subiu  $t5, $t0, -2

li     $ra, 36 
add    $t6, $t0, $t1 
li     $ra, 44 
sub    $t7, $t1, $t0 
li     $ra, 52 
addi   $t8, $t0, 2 
li     $ra, 60 
subi   $t9, $t0, -2
.word  0xfeedfeed

.space 0x7fc4

jr     $ra
addi   $s0, $s0, 1
