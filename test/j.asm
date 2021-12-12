.set noreorder
           li  $sp, 80
           li  $t0, 5 
           li  $t1, 3 
           li  $s0, 4
           jal  add_shift
           addi $ra, $ra, 4
end:       .word 0xfeedfeed
           j end
           add  $t9, $s0, $t0 

add_shift:
           subu  $sp, $sp, 4 
           sw    $s0, ($sp)
           addu  $s0, $t0, $t1 
           sll   $t0, $s0, 3 
           lw    $s0, ($sp) 
           jr    $ra 
           addi  $sp, $sp, 4



