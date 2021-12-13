li     $t0, 0x7fffffff
li     $t1, 2
li     $t9, -3
addu   $t2, $t0, $t1 
subu   $t3, $t0, $t9 
addiu  $t4, $t0, 4

li     $ra, 32 
add    $t5, $t0, $t1 
li     $ra, 40 
sub    $t6, $t1, $t0 
li     $ra, 48 
addi   $t7, $t0, 2 
.word  0xfeedfeed

.space 0x7fd0

jr     $ra
addi   $s0, $s0, 1
