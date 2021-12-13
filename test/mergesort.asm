	.text

	la	$a0, info		# Load the start address of the array
	lw	$t0, length		# Load the array length
	sll	$t0, $t0, 2		# Multiple the array length by 4 (the size of the elements)
	add	$a1, $a0, $t0		# Calculate the array end address
	jal	mergesort		# Call the merge sort function
  	b	sortend			# We are finished sorting
	
##
# Recrusive mergesort function
#
# @param $a0 first address of the array
# @param $a1 last address of the array
##
mergesort:

	addi	$sp, $sp, -16		# Adjust stack pointer
	sw	$ra, 0($sp)		# Store the return address on the stack
	sw	$a0, 4($sp)		# Store the array start address on the stack
	sw	$a1, 8($sp)		# Store the array end address on the stack
	
	sub 	$t0, $a1, $a0		# Calculate the difference between the start and end address (i.e. number of elements * 4)

	ble	$t0, 4, mergesortend	# If the array only contains a single element, just return
	
	srl	$t0, $t0, 3		# Divide the array size by 8 to half the number of elements (shift right 3 bits)
	sll	$t0, $t0, 2		# Multiple that number by 4 to get half of the array size (shift left 2 bits)
	add	$a1, $a0, $t0		# Calculate the midpoint address of the array
	sw	$a1, 12($sp)		# Store the array midpoint address on the stack
	
	jal	mergesort		# Call recursively on the first half of the array
	
	lw	$a0, 12($sp)		# Load the midpoint address of the array from the stack
	lw	$a1, 8($sp)		# Load the end address of the array from the stack
	
	jal	mergesort		# Call recursively on the second half of the array
	
	lw	$a0, 4($sp)		# Load the array start address from the stack
	lw	$a1, 12($sp)		# Load the array midpoint address from the stack
	lw	$a2, 8($sp)		# Load the array end address from the stack
	
	jal	merge			# Merge the two array halves
	
mergesortend:				

	lw	$ra, 0($sp)		# Load the return address from the stack
	addi	$sp, $sp, 16		# Adjust the stack pointer
	jr	$ra			# Return 
	
##
# Merge two sorted, adjacent arrays into one, in-place
#
# @param $a0 First address of first array
# @param $a1 First address of second array
# @param $a2 Last address of second array
##
merge:
	addi	$sp, $sp, -16		# Adjust the stack pointer
	sw	$ra, 0($sp)		# Store the return address on the stack
	sw	$a0, 4($sp)		# Store the start address on the stack
	sw	$a1, 8($sp)		# Store the midpoint address on the stack
	sw	$a2, 12($sp)		# Store the end address on the stack
	
	move	$s0, $a0		# Create a working copy of the first half address
	move	$s1, $a1		# Create a working copy of the second half address
	
mergeloop:

	lw	$t0, 0($s0)		# Load the first half position pointer
	lw	$t1, 0($s1)		# Load the second half position pointer
	lw	$t0, 0($t0)		# Load the first half position value
	lw	$t1, 0($t1)		# Load the second half position value
	
	bgt	$t1, $t0, noshift	# If the lower value is already first, don't shift
	
	move	$a0, $s1		# Load the argument for the element to move
	move	$a1, $s0		# Load the argument for the address to move it to
	jal	shift			# Shift the element to the new position 
	
	addi	$s1, $s1, 4		# Increment the second half index
noshift:
	addi	$s0, $s0, 4		# Increment the first half index
	
	lw	$a2, 12($sp)		# Reload the end address
	bge	$s0, $a2, mergeloopend	# End the loop when both halves are empty
	bge	$s1, $a2, mergeloopend	# End the loop when both halves are empty
	b	mergeloop
	
mergeloopend:
	
	lw	$ra, 0($sp)		# Load the return address
	addi	$sp, $sp, 16		# Adjust the stack pointer
	jr 	$ra			# Return

##
# Shift an array element to another position, at a lower address
#
# @param $a0 address of element to shift
# @param $a1 destination address of element
##
shift:
	li	$t0, 10
	ble	$a0, $a1, shiftend	# If we are at the location, stop shifting
	addi	$t6, $a0, -4		# Find the previous address in the array
	lw	$t7, 0($a0)		# Get the current pointer
	lw	$t8, 0($t6)		# Get the previous pointer
	sw	$t7, 0($t6)		# Save the current pointer to the previous address
	sw	$t8, 0($a0)		# Save the previous pointer to the current address
	move	$a0, $t6		# Shift the current position back
	b 	shift			# Loop again
shiftend:
	jr	$ra			# Return
	
sortend:				# Point to jump to when sorting is complete


# Print out the indirect array
	li	$t0, 0				# Initialize the current index
prloop:
	lw	$t1,length			# Load the array length
	bge	$t0,$t1,prdone			# If we hit the end of the array, we are done
	sll	$t2,$t0,2			# Multiply the index by 4 (2^2)
	lw	$t3,info($t2)			# Get the pointer
	lw	$a0,0($t3)			# Get the value pointed to and store it for printing
	li	$v0,1				
	syscall					# Print the value
	la	$a0,eol				# Set the value to print to the newline
	li	$v0,4				
	syscall					# Print the value
	addi	$t0,$t0,1			# Increment the current index
	b	prloop				# Run through the print block again
prdone:						# We are finished
	li	$v0,10
	syscall
	.data
eol:	.asciiz	"\n"


# Some test data
eight:	.word	8
five:	.word	5
four:	.word	4
nine:	.word	9
one:	.word	1
seven:	.word	7
six:	.word	6
ten:	.word	10
three:	.word	3
two:	.word	2

# An array of pointers (indirect array)
length:	.word	10	# Array length
info:	.word	seven
	.word	three
	.word	ten
	.word	one
	.word	five
	.word	two
	.word	nine
	.word	eight
	.word	four
	.word	six