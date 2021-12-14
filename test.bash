for value in feed_end add_immediate and_immediate r load store branch j midterm fib load_use invalid_instruction arithmetic_exception
do
    echo $value
    bin/mips-linux-gnu-as test/$value.asm -o $value.elf
    bin/mips-linux-gnu-objcopy $value.elf -j .text -O binary $value.bin
    ./sim $value.bin 
    sleep 0.25s
    diff -y reg_state.out test/${value}_reg_state.out
    mv mem_state.out ${value}_mem_state.out 
    mv reg_state.out ${value}_reg_state.out
done

diff -y fib_mem_state.out test/fib_mem_state.out
diff -y store_mem_state.out test/store_mem_state.out
