#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"

#define MAGIC_DEMARC 0xfeedfeed
#define EXCEPTION_ADDR 0x8000

//Note that an instruction that modifies the PC will never throw an
//exception or be prone to errors from the memory abstraction.
//Thus a single value is enough to depict the status of an instruction.
#define NOINC_PC 1
#define OVERFLOW 2
#define ILLEGAL_INST 3

//TODO: Fix the error messages to output the correct PC in case of errors.

extern void dumpRegisterStateInternal(RegisterInfo &reg, std::ostream &reg_out);

enum REG_IDS
{
    REG_ZERO,
    REG_AT,
    REG_V0,
    REG_V1,
    REG_A0,
    REG_A1,
    REG_A2,
    REG_A3,
    REG_T0,
    REG_T1,
    REG_T2,
    REG_T3,
    REG_T4,
    REG_T5,
    REG_T6,
    REG_T7,
    REG_S0,
    REG_S1,
    REG_S2,
    REG_S3,
    REG_S4,
    REG_S5,
    REG_S6,
    REG_S7,
    REG_T8,
    REG_T9,
    REG_K0,
    REG_K1,
    REG_GP,
    REG_SP,
    REG_FP,
    REG_RA,
    NUM_REGS
};

enum OP_IDS
{
    //R-type opcodes...
    OP_ZERO = 0,
    //I-type opcodes...
    OP_ADDI = 0x8,
    OP_ADDIU = 0x9,
    OP_ANDI = 0xc,
    OP_BEQ = 0x4,
    OP_BNE = 0x5,
    OP_BLEZ = 0x06,
    OP_BGTZ = 0x07,
    OP_LBU = 0x24,
    OP_LHU = 0x25,
    OP_LL = 0x30,
    OP_LUI = 0xf,
    OP_LW = 0x23,
    OP_ORI = 0xd,
    OP_SLTI = 0xa,
    OP_SLTIU = 0xb,
    OP_SB = 0x28,
    OP_SC = 0x38,
    OP_SH = 0x29,
    OP_SW = 0x2b,
    //J-type opcodes...
    OP_J = 0x2,
    OP_JAL = 0x3
};

enum FUN_IDS
{
    FUN_ADD = 0x20,
    FUN_ADDU = 0x21,
    FUN_AND = 0x24,
    FUN_JR = 0x08,
    FUN_NOR = 0x27,
    FUN_OR = 0x25,
    FUN_SLT = 0x2a,
    FUN_SLTU = 0x2b,
    FUN_SLL = 0x00,
    FUN_SRL = 0x02,
    FUN_SUB = 0x22,
    FUN_SUBU = 0x23
};

using namespace std;

//Static global variables...
static uint32_t progCounter;
static uint32_t regs[NUM_REGS];
static MemoryStore *mem;

static bool ll_sc_flag;
static uint32_t ll_sc_addr;

int initMemory(ifstream &inputProg)
{
    if (inputProg && mem)
    {
        uint32_t curVal = 0;
        uint32_t addr = 0;

        while (inputProg.read((char *)(&curVal), sizeof(uint32_t)))
        {
            curVal = ConvertWordToBigEndian(curVal);
            int ret = mem->setMemValue(addr, curVal, WORD_SIZE);

            if (ret)
            {
                cout << "Could not set memory value!" << endl;
                return -EINVAL;
            }

            //We're reading 4 bytes each time...
            addr += 4;
        }
    }
    else
    {
        cout << "Invalid file stream or memory image passed, could not initialise memory values" << endl;
        return -EINVAL;
    }

    return 0;
}

//Byte's the smallest thing that can hold the opcode...
uint8_t getOpcode(uint32_t instr)
{
    return (uint8_t)((instr >> 26) & 0x3f);
}

uint8_t getSign(uint32_t value)
{
    return (value >> 31) & 0x1;
}

int doAddSub(uint8_t rd, uint32_t s1, uint32_t s2, bool isAdd, bool checkOverflow)
{
    bool overflow = false;
    int32_t result = 0;

    if (isAdd)
    {
        result = static_cast<int32_t>(s1) + static_cast<int32_t>(s2);
    }
    else
    {
        result = static_cast<int32_t>(s1) - static_cast<int32_t>(s2);
    }

    if (checkOverflow)
    {
        if (isAdd)
        {
            overflow = getSign(s1) == getSign(s2) && getSign(s2) != getSign(result);
        }
        else
        {
            overflow = getSign(s1) != getSign(s2) && getSign(s2) == getSign(result);
        }
    }

    if (overflow)
    {
        //Inform the caller that overflow occurred so it can take appropriate action.
        return OVERFLOW;
    }

    //Otherwise update state and return success.
    regs[rd] = static_cast<uint32_t>(result);

    return 0;
}

int runDelayInstruction(uint32_t delayPC, int succRet);

int handleOpZeroInst(uint32_t instr)
{
    uint8_t rs = (instr >> 21) & 0x1f;
    uint8_t rt = (instr >> 16) & 0x1f;
    uint8_t rd = (instr >> 11) & 0x1f;
    uint8_t shamt = (instr >> 6) & 0x1f;
    uint8_t funct = instr & 0x3f;

    int ret = 0;
    uint32_t oldPC = progCounter;

    switch (funct)
    {
    case FUN_ADD:
        ret = doAddSub(rd, regs[rs], regs[rt], true, true);
        break;
    case FUN_ADDU:
        //No overflow...
        ret = doAddSub(rd, regs[rs], regs[rt], true, false);
        break;
    case FUN_AND:
        regs[rd] = regs[rs] & regs[rt];
        break;
    case FUN_JR:
        progCounter = regs[rs];
        ret = NOINC_PC;
        break;
    case FUN_NOR:
        regs[rd] = ~(regs[rs] | regs[rt]);
        break;
    case FUN_OR:
        regs[rd] = regs[rs] | regs[rt];
        break;
    case FUN_SLT:
        regs[rd] = (static_cast<int32_t>(regs[rs]) < static_cast<int32_t>(regs[rt])) ? 1 : 0;
        break;
    case FUN_SLTU:
        regs[rd] = (regs[rs] < regs[rt]) ? 1 : 0;
        break;
    case FUN_SLL:
        regs[rd] = regs[rt] << shamt;
        break;
    case FUN_SRL:
        regs[rd] = regs[rt] >> shamt;
        break;
    case FUN_SUB:
        ret = doAddSub(rd, regs[rs], regs[rt], false, true);
        break;
    case FUN_SUBU:
        //No overflow...
        ret = doAddSub(rd, regs[rs], regs[rt], false, false);
        break;
    default:
        //Illegal instruction. Trigger an exception.
        ret = ILLEGAL_INST;
        cerr << "Illegal instruction at address "
             << "0x" << hex
             << setfill('0') << setw(8) << progCounter << endl;
        break;
    }

    //Reset the zero register...
    regs[REG_ZERO] = 0;

    //Did this instruction throw an exception or fail? If so, just return that
    //and do nothing further.
    if (ret && ret != NOINC_PC)
    {
        return ret;
    }

    //Did this instruction modify the PC? If so, execute the instruction
    //in the delay slot and then return the return value of the immediate
    //instruction's execution unless the delay slot instruction throws an exception,
    //in which case just return the exception of that delay instruction.
    if (ret == NOINC_PC)
    {
        return runDelayInstruction(oldPC + 4, NOINC_PC);
    }

    //The ret value must be 0 here, or something's amiss.
    if (ret != 0)
    {
        cerr << "ret was nonzero - check logic!" << endl;
    }

    return 0;
}

int doLoad(uint32_t addr, MemEntrySize size, uint8_t rt)
{
    uint32_t value = 0;
    int ret = 0;
    ret = mem->getMemValue(addr, value, size);
    if (ret)
    {
        cout << "Could not get mem value" << endl;
        return ret;
    }

    switch (size)
    {
    case BYTE_SIZE:
        regs[rt] = value & 0xFF;
        break;
    case HALF_SIZE:
        regs[rt] = value & 0xFFFF;
        break;
    case WORD_SIZE:
        regs[rt] = value;
        break;
    default:
        cerr << "Invalid size passed, cannot read/write memory" << endl;
        return -EINVAL;
    }

    return 0;
}

void checkLLSCOverlap(uint32_t addr, MemEntrySize size)
{
    if (!ll_sc_flag)
    {
        //Atomicity either doesn't need to be checked or has already been broken,
        //so there's nothing to do here.
        return;
    }

    uint32_t store_start = addr;
    uint32_t store_end = addr + static_cast<uint32_t>(size);
    uint32_t ll_sc_start = ll_sc_addr;
    uint32_t ll_sc_end = ll_sc_addr + static_cast<uint32_t>(WORD_SIZE);

    if ((store_start >= ll_sc_start && store_start < ll_sc_end) ||
        (store_end > ll_sc_start && store_end <= ll_sc_end))
    {
        //We have an overlap.
        ll_sc_flag = false;
    }
}

//TODO: Do address calculations that overflow cause an overflow exception?
//Probably not, because memory is always addressed by UNSIGNED numbers, not signed ones.
int handleImmInst(uint32_t instr)
{
    uint8_t opcode = (instr >> 26) & 0x3f;
    uint8_t rs = (instr >> 21) & 0x1f;
    uint8_t rt = (instr >> 16) & 0x1f;
    uint16_t imm = instr & 0xffff;
    //Sign extend the immediate...
    uint32_t seImm = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(imm)));
    uint32_t zeImm = imm;

    int ret = 0;
    uint32_t addr = static_cast<uint32_t>(static_cast<int32_t>(regs[rs]) + static_cast<int32_t>(seImm));
    uint32_t value = 0;
    uint32_t oldPC = progCounter;

    switch (opcode)
    {
    case OP_ADDI:
        ret = doAddSub(rt, regs[rs], seImm, true, true);
        break;
    case OP_ADDIU:
        ret = doAddSub(rt, regs[rs], seImm, true, false);
        break;
    case OP_ANDI:
        regs[rt] = regs[rs] & zeImm;
        break;
    case OP_BEQ:
        //Note that signs don't matter when you're checking for equality :).
        if (regs[rs] == regs[rt])
        {
            //MIPS multiplies immediates by 4 for branches...
            progCounter += 4 + ((static_cast<int32_t>(seImm)) << 2);
            //Note that if the branch is not taken, we don't need to do anything with
            //regard to delay slots. The instruction after the branch will be executed
            //as required by the regular straight-line execution logic.
            ret = NOINC_PC;
        }
        break;
    case OP_BNE:
        //See also notes for BEQ above.
        if (regs[rs] != regs[rt])
        {
            progCounter += 4 + ((static_cast<int32_t>(seImm)) << 2);
            ret = NOINC_PC;
        }
        break;
    case OP_LBU:
        ret = doLoad(addr, BYTE_SIZE, rt);
        break;
    case OP_LHU:
        ret = doLoad(addr, HALF_SIZE, rt);
        break;
    case OP_LL:
        //Set the ll_sc_flag. It'll be cleared on any exception or when the SC succeeds,
        //or if there's an intervening store that overlaps with the ll word in any way.
        ll_sc_flag = true;
        ll_sc_addr = addr;
        ret = doLoad(addr, WORD_SIZE, rt);
        break;
    case OP_LUI:
        regs[rt] = static_cast<uint32_t>(imm) << 16;
        break;
    case OP_LW:
        ret = doLoad(addr, WORD_SIZE, rt);
        break;
    case OP_ORI:
        regs[rt] = regs[rs] | zeImm;
        break;
    case OP_SLTI:
        regs[rt] = (static_cast<int32_t>(regs[rs]) < static_cast<int32_t>(seImm)) ? 1 : 0;
        break;
    case OP_SLTIU:
        regs[rt] = (regs[rs] < static_cast<uint32_t>(seImm)) ? 1 : 0;
        break;
    case OP_SB:
        ret = mem->setMemValue(addr, regs[rt] & 0xFF, BYTE_SIZE);
        checkLLSCOverlap(addr, BYTE_SIZE);
        break;
    case OP_SC:
        if (addr == ll_sc_addr)
        {
            if (ll_sc_flag)
            {
                //We are atomic. Store the value.
                ret = mem->setMemValue(addr, regs[rt], WORD_SIZE);
            }

            regs[rt] = (ll_sc_flag) ? 1 : 0;
        }
        else
        {
            regs[rt] = 0;
        }
        ll_sc_flag = false;
        break;
    case OP_SH:
        ret = mem->setMemValue(addr, regs[rt] & 0xFFFF, HALF_SIZE);
        checkLLSCOverlap(addr, HALF_SIZE);
        break;
    case OP_SW:
        ret = mem->setMemValue(addr, regs[rt], WORD_SIZE);
        checkLLSCOverlap(addr, WORD_SIZE);
        break;
    }

    //Reset the zero register...
    regs[REG_ZERO] = 0;

    //Did this instruction throw an exception or fail? If so, just return that
    //and do nothing further.
    if (ret && ret != NOINC_PC)
    {
        return ret;
    }

    //Did this instruction modify the PC? If so, execute the instruction
    //in the delay slot and then return the return value of the immediate
    //instruction's execution unless the delay slot instruction throws an exception,
    //in which case just return the exception of that delay instruction.
    if (ret == NOINC_PC)
    {
        return runDelayInstruction(oldPC + 4, NOINC_PC);
    }

    //The ret value must be 0 here, or something's amiss.
    if (ret != 0)
    {
        cerr << "ret was nonzero - check logic!" << endl;
    }

    return 0;
}

int handleJInst(uint32_t instr)
{
    uint8_t opcode = (instr >> 26) & 0x3f;
    uint32_t addr = instr & 0x3ffffff;
    uint32_t oldPC = progCounter;

    switch (opcode)
    {
    case OP_JAL:
        regs[REG_RA] = progCounter + 8;
        //fall through
    case OP_J:
        progCounter = ((progCounter + 4) & 0xf0000000) | (addr << 2);
        break;
    }

    //Reset the zero register...
    regs[REG_ZERO] = 0;

    //Execute the instruction in the delay slot and then return the
    //return value of the branch's execution
    //(which is always NOINC_PC because J instrs always modify the PC)
    //unless the delay slot instruction throws an exception,
    //in which case just return the exception of that delay instruction.
    return runDelayInstruction(oldPC + 4, NOINC_PC);
}

int runInstruction(uint32_t curInst, bool isDelayInst);

int runDelayInstruction(uint32_t delayPC, int succRet)
{
    uint32_t delayInst = 0;
    int ret = mem->getMemValue(delayPC, delayInst, WORD_SIZE);
    if (ret)
    {
        return ret;
    }

    ret = runInstruction(delayInst, true);

    if (ret)
    {
        return ret;
    }

    return succRet;
}

int runInstruction(uint32_t curInst)
{
    runInstruction(curInst, false);
}

int runInstruction(uint32_t curInst, bool isDelayInst)
{
    int ret = 0;

    switch (getOpcode(curInst))
    {
    //Everything with a zero opcode...
    case OP_ZERO:
        ret = handleOpZeroInst(curInst);
        break;
    case OP_ADDI:
    case OP_ADDIU:
    case OP_ANDI:
    case OP_BEQ:
    case OP_BNE:
    case OP_LBU:
    case OP_LHU:
    case OP_LL:
    case OP_LUI:
    case OP_LW:
    case OP_ORI:
    case OP_SLTI:
    case OP_SLTIU:
    case OP_SB:
    case OP_SC:
    case OP_SH:
    case OP_SW:
        ret = handleImmInst(curInst);
        break;
    case OP_J:
    case OP_JAL:
        ret = handleJInst(curInst);
        break;
    default:
        //Illegal instruction. Trigger an exception.
        //Note: Since we catch illegal instructions here, the "handle"
        //instructions don't need to check for illegal instructions.
        //except for the case with a 0 opcode and illegal function.
        ret = ILLEGAL_INST;
        cerr << "Illegal instruction at address "
             << "0x" << hex
             << setfill('0') << setw(8) << progCounter << endl;
        break;
    }

    if (ret == NOINC_PC)
    {
        //Don't increment the PC.
        return 0;
    }

    if (ret == OVERFLOW || ret == ILLEGAL_INST)
    {
        //There was an exception. Clear the LL/SC flag, set the PC to the
        //exception address and return 0. This is because nothing
        //special needs to be done by the calling code except to not increment
        //the PC - it should just continue execution from what the PC is like
        //normal. In the case of a regular (non-delay) instruction, this is exactly
        //what runProgram does. In the case of a delay instruction, returning 0 here
        //causes the branch before the delay instruction to return NOINC_PC, which
        //will result in the PC not getting incremented and execution continuing from
        //whatever the PC is (which is now the exception address), which is exactly
        //what we want.
        //Note that this is the only function where progCounter is incremented
        //(barring the nop case in runProgram).
        ll_sc_flag = false;
        progCounter = EXCEPTION_ADDR;
        return 0;
    }

    if (!isDelayInst)
    {
        progCounter += 4;
    }

    return ret;
}

void fillRegisterState(RegisterInfo &reg)
{
    reg.at = regs[REG_AT];

    for (int i = 0; i < V_REG_SIZE; i++)
    {
        reg.v[i] = regs[i + REG_V0];
    }

    for (int i = 0; i < A_REG_SIZE; i++)
    {
        reg.a[i] = regs[i + REG_A0];
    }

    //Remember, t8 and t9 are handled separately...
    for (int i = 0; i < T_REG_SIZE - 2; i++)
    {
        reg.t[i] = regs[i + REG_T0];
    }

    for (int i = 0; i < S_REG_SIZE; i++)
    {
        reg.s[i] = regs[i + REG_S0];
    }

    //t8 and t9...
    for (int i = 0; i < 2; i++)
    {
        reg.t[i + 8] = regs[i + REG_T8];
    }

    for (int i = 0; i < K_REG_SIZE; i++)
    {
        reg.k[i] = regs[i + REG_K0];
    }

    reg.gp = regs[REG_GP];
    reg.sp = regs[REG_SP];
    reg.fp = regs[REG_FP];
    reg.ra = regs[REG_RA];
}

//For delayed branches in combination with self-modifying code *shudder*, we should be
//fine. Each instruction is fetched only once all previous instructions have finished
//execution, so there should be no problem with stale values, etc.
int runProgram()
{
    while (true)
    {
        uint32_t curInst = 0;
        //Store the current PC for printing out errors...
        uint32_t curPC = progCounter;

        if (mem->getMemValue(progCounter, curInst, WORD_SIZE))
        {
            return -EBADF;
        }

        //Check for the end of the code segment.
        if (curInst == MAGIC_DEMARC)
        {
            break;
        }

        int ret = runInstruction(curInst);

        if (ret)
        {
            //There was an error executing the instruction.
            //Note that this won't give appropriate info for delayed branches...TODO: fix this...
            cerr << "Error executing instruction "
                 << "0x" << hex << setfill('0')
                 << setw(8) << curInst << " at address "
                 << "0x" << curPC << endl;
            return -EINVAL;
        }

        //Dump the state of the system after every instruction for debugging purposes.
        //Commented out by default.
        /*cout << endl;
        cout << "Finished executing instruction " << "0x" << hex << setfill('0')
             << setw(8) << curInst << " at address " << "0x" << curPC << endl;
        RegisterInfo reg;
        memset(&reg, 0, sizeof(RegisterInfo));
        fillRegisterState(reg);
        dumpRegisterStateInternal(reg, std::cout);
        dumpMemoryState(mem);*/

        //The PC will be appropriately set by runInstruction.
        //We don't have to do anything here.
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        cout << "Usage: ./sim <file name>" << endl;
        return -EINVAL;
    }

    ifstream prog;
    prog.open(argv[1], ios::binary | ios::in);

    mem = createMemoryStore();

    if (initMemory(prog))
    {
        return -EBADF;
    }

    for (int i = 0; i < NUM_REGS; i++)
    {
        //This'll initialise the zero register appropriately too...
        regs[i] = 0;
    }

    //Run the program...
    progCounter = 0;
    ll_sc_flag = false;

    runProgram();

    //Set the register values in the struct for printing...
    RegisterInfo reg;
    memset(&reg, 0, sizeof(RegisterInfo));
    fillRegisterState(reg);

    dumpRegisterState(reg);
    dumpMemoryState(mem);

    delete mem;
    return 0;
}

#include "CacheConfig.h"
#include "DriverFunctions.h"

enum INST_TYPE
{
    R,
    I,
    J
};

struct RData
{
    uint8_t opcode;
    uint32_t rsValue;
    uint32_t rtValue;
    uint8_t rs;
    uint8_t rt;
    uint8_t rd;
    uint8_t shamt;
    uint8_t funct;
};
struct IData
{
    uint8_t opcode;
    uint32_t rsValue;
    uint32_t rtValue;
    uint8_t rs;
    uint8_t rt;
    uint16_t imm;
    uint32_t seImm;
    uint32_t zeImm;
};
struct JData
{
    uint8_t opcode;
    uint32_t addr;
    uint32_t oldPC;
};

struct InstructionData
{
    union
    {
        RData rData;
        IData iData;
        JData jData;
    } data;
    INST_TYPE tag;

    uint8_t rs() {
        switch (this->tag) {
        case R:
            return this->data.rData.rs;
        case I:
            return this->data.iData.rs;
        case J:
            return 0;
        }
    }

    uint8_t rt() {
        switch (this->tag) {
        case R:
            return this->data.rData.rt;
        case I:
            return this->data.iData.rt;
        case J:
            return 0;
        }
    }

    void rsValue(uint64_t val) {
        switch (this->tag) {
        case R:
            this->data.rData.rsValue = val;
            break;
        case I:
            this->data.iData.rsValue = val;
            break;
        case J:
            break;
        }
    }

    void rtValue(uint64_t val) {
        switch (this->tag) {
        case R:
            this->data.rData.rtValue = val;
            break;
        case I:
            this->data.iData.rtValue = val;
            break;
        case J:
            break;
        }
    }
};

struct IFID
{
    uint32_t pc;
    uint32_t instruction;
};

struct IDEX
{
    uint32_t instruction;
    InstructionData instructionData;
};

struct EXMEM
{
    uint32_t instruction;
    InstructionData instructionData;
    uint64_t regWriteValue = UINT64_MAX;
    uint8_t regToWrite;
};

using MEMWB = EXMEM;

struct Cache
{
};

uint8_t getOpcode(uint32_t instr)
{
    return (instr >> 21) & 0x1f;
}

// Arg: current opcode
// Return: enum INST_TYPE tag associated with instruction being executed
enum INST_TYPE getInstType(uint32_t instr)
{
    uint8_t opcode = getOpcode(instr);
    switch (opcode)
    {
    case OP_ZERO:
        return R;
        break;
    case OP_ADDI:
    case OP_ADDIU:
    case OP_ANDI:
    case OP_BEQ:
    case OP_BNE:
    case OP_LBU:
    case OP_LHU:
    case OP_LL:
    case OP_LUI:
    case OP_LW:
    case OP_ORI:
    case OP_SLTI:
    case OP_SLTIU:
    case OP_SB:
    case OP_SC:
    case OP_SH:
    case OP_SW:
        return I;
        break;
    case OP_J:
    case OP_JAL:
        return J;
        break;
    }
}

// Arg: current instruction
// Return: struct RData holding relevant register instruction data
struct RData getRData(uint32_t instr)
{
    uint8_t rs = (instr >> 21) & 0x1f;
    uint8_t rt = (instr >> 16) & 0x1f;
    struct RData rData = {
        getOpcode(instr), // uint8_t opcode;
        regs[rs],         // uint32_t rsValue;
        regs[rt],         // uint32_t rtValue;
        rs,
        rt,
        (instr >> 11) & 0x1f, // uint8_t rd
        (instr >> 6) & 0x1f,  // uint8_t shamt
        instr & 0x3f          // uint8_tfunct
    };

    return rData;
}

// Arg: current instruction
// Return: struct IData holding relevant immmediate instruction data
struct IData getIData(uint32_t instr)
{
    uint8_t rs = (instr >> 21) & 0x1f;
    uint8_t rt = (instr >> 16) & 0x1f;
    uint16_t imm = instr & 0xffff;
    struct IData iData = {
        getOpcode(instr),                                                       // uint8_t opcode;
        regs[rs],                                                               // uint32_t rsValue;
        regs[rt],                                                               // uint32_t rtValue;
        rs,                                                                     // uint8_t rs;
        rt,                                                                     // uint8_t rt;
        imm,                                                                    // uint16_t imm;
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(imm))), // uint32_t seImm;
        imm,                                                                    // uint32_t zeImm;
    };

    return iData;
}

// Arg: current instruction
// Return: struct JData holding relevant jump instruction data
struct JData getJData(uint32_t instr)
{
    struct JData jData = {
        getOpcode(instr),  // uint8_t opcode;
        instr & 0x3ffffff, // uint32_t addr;
        ifid.pc            // uint32_t oldPC;
    };

    return jData;
}

// returns UINT64_MAX if result is not available in the cache yet, the value at the address otherwise
uint64_t getCacheValue(Cache *cache, MemoryStore *mem, uint64_t cycle, uint32_t addr, MemEntrySize size)
{
    uint32_t value = 0;
    int ret = mem->getMemValue(addr, value, size);
    if (ret)
    {
        cout << "Could not get mem value" << endl;
        exit(1);
    }

    switch (size)
    {
    case BYTE_SIZE:
        return value & 0xFF;
        break;
    case HALF_SIZE:
        return value & 0xFFFF;
        break;
    case WORD_SIZE:
        return value;
        break;
    default:
        cerr << "Invalid size passed, cannot read/write memory" << endl;
        exit(1);
    }
}

Cache icache;
Cache dcache;
PipeState pipeState;
uint32_t pc;
MemoryStore *memStore;
IFID ifid;
IDEX idex;
EXMEM exmem;
MEMWB memwb;

int initSimulator(CacheConfig &icConfig, CacheConfig &dcConfig, MemoryStore *mainMem)
{
    // TODO: initialize icache, dcache
    memStore = mainMem;
}

void runCycle()
{
    IFID nextIfid;
    IDEX nextIdex;
    EXMEM nextExmem;
    MEMWB nextMemwb;

    // instructionFetch
    auto instruction = getCacheValue(&dcache, memStore, pipeState.cycle, pc, MemEntrySize::WORD_SIZE);
    if (instruction == UINT64_MAX)
    {
        // TODO: stall
    }
    pc += 4;
    nextIfid.instruction = instruction;
    nextIfid.pc = pc;

    // instructionDecode
    idex.instructionData.tag = getInstType(ifid.instruction);
    switch (nextIdex.instructionData.tag)
    {
    case R:
    {
        nextIdex.instructionData.data.rData = getRData(ifid.instruction);
        break;
    }
    case I:
    {
        auto iData = getIData(ifid.instruction);
        switch (iData.opcode)
        {
        case OP_BEQ:
            if (iData.rsValue == iData.rtValue)
            {
                pc = ifid.pc + (iData.imm << 2);
                break;
            }
            break;
        case OP_BNE:
            if (iData.rsValue != iData.rtValue)
            {
                pc = ifid.pc + (iData.imm << 2);
                break;
            }
            break;
        case OP_BGTZ:
            if (iData.rsValue > 0)
            {
                pc = ifid.pc + (iData.imm << 2);
                break;
            }
            break;
        case OP_BLEZ:
            if (iData.rsValue <= 0)
            {
                pc = ifid.pc + (iData.imm << 2);
                break;
            }
            break;
        default:
        }
        nextIdex.instructionData.data.iData = iData;
        break;
    }
    case J:
    {
        auto jData = getJData(ifid.instruction);
        switch (jData.opcode)
        {
        case OP_JAL:
            // TODO: what to do, WB is weird
        case OP_J:
            pc = ((ifid.pc + 4) & 0xf0000000) | (jData.addr << 2);
            break;

        default:
        }
        nextIdex.instructionData.data.jData = jData;
        break;
    }
    }
    nextIdex.instruction = ifid.instruction;

    // execute

    // forwarding of results from previous cycle's execute
    if (exmem.regWriteValue != UINT64_MAX && exmem.regToWrite != 0) {
        if (exmem.regToWrite == idex.instructionData.rs()) {
            idex.instructionData.rsValue(exmem.regWriteValue);
        }
        if (exmem.regToWrite == exmem.instructionData.rt()) {
            exmem.instructionData.rtValue(exmem.regWriteValue);
        }
    }

    switch (idex.instructionData.tag)
    {
    case R:
        nextExmem.regWriteValue = handleRInstEx(idex.instructionData.data.rData);
        nextMemwb.regToWrite = idex.instructionData.data.rData.rd;
        break;
    case I:
        nextExmem.regWriteValue = handleImmInstEx(idex.instructionData.data.iData);
        nextMemwb.regToWrite = idex.instructionData.data.iData.rt;
        break;
    case J:
    {
        handleJInstEx(idex.instructionData.data.jData);
        break;
    }
    }
    nextExmem.instruction = idex.instruction;
    nextExmem.instructionData = idex.instructionData;

    // mem
    switch (exmem.instructionData.tag) {
        case I:
            // TODO: handle stalls
            handleMem(exmem.instructionData.data.iData);
    }
    nextMemwb = exmem;

    // writeBack
    if (memwb.regWriteValue != UINT64_MAX && memwb.regToWrite != 0) {
        regs[memwb.regToWrite] = memwb.regWriteValue;
    }
    
    // finish cycle
    ifid = nextIfid;
    idex = nextIdex;
    exmem = nextExmem;
    memwb = nextMemwb;
}

// returns new value of rd, or UINT64_MAX if none
uint64_t handleRInstEx(RData &rData)
{
    uint64_t rdValue = UINT64_MAX;
    switch (rData.funct)
    {
    case FUN_ADD:
    case FUN_ADDU:
        rdValue = rData.rsValue + rData.rtValue;
        break;
    case FUN_AND:
        rdValue = rData.rsValue & rData.rtValue;
        break;
    case FUN_JR:
        // progCounter = regs[rs];
        // ret = NOINC_PC;
        // TODO!!
        break;
    case FUN_NOR:
        rdValue = ~(rData.rsValue | rData.rtValue);
        break;
    case FUN_OR:
        rdValue = rData.rsValue | rData.rtValue;
        break;
    case FUN_SLT:
        rdValue = (static_cast<int32_t>(rData.rsValue) < static_cast<int32_t>(rData.rtValue)) ? 1 : 0;
        break;
    case FUN_SLTU:
        rdValue = (rData.rsValue < rData.rtValue) ? 1 : 0;
        break;
    case FUN_SLL:
        rdValue = rData.rtValue << rData.shamt;
        break;
    case FUN_SRL:
        rdValue = rData.rsValue >> rData.shamt;
        break;
    case FUN_SUB:
    case FUN_SUBU:
        rdValue = rData.rsValue = rData.rtValue;
        break;
    default:
        cerr << "Illegal instruction at address "
             << "0x" << hex
             << setfill('0') << setw(8) << progCounter << endl;
        exit(1);
        break;
    }

    return rdValue;
}

// returns new value of rt destination, or UINT64_MAX if no new value
uint64_t handleImmInstEx(IData &iData)
{
    uint64_t rtValue = UINT64_MAX;

    switch (iData.opcode)
    {
    case OP_ADDI:
    case OP_ADDIU:
        rtValue = iData.rsValue + iData.seImm;
        break;
    case OP_ANDI:
        rtValue = iData.rsValue & iData.zeImm;
        break;
    case OP_LBU:
    case OP_LHU:
    case OP_LW:
        // loads happen in mem stage
        break;
    case OP_LUI:
        rtValue = static_cast<uint32_t>(iData.imm) << 16;
        break;
    case OP_ORI:
        rtValue = iData.rsValue | iData.zeImm;
        break;
    case OP_SLTI:
        rtValue = (static_cast<int32_t>(iData.rsValue) < static_cast<int32_t>(iData.seImm)) ? 1 : 0;
        break;
    case OP_SLTIU:
        rtValue = (iData.rsValue < static_cast<uint32_t>(iData.seImm)) ? 1 : 0;
        break;
    case OP_SB:
    case OP_SH:
    case OP_SW:
        // stores happen in mem stage
        break;
    }

    return rtValue;
}

int handleJInstEx(JData &jData)
{
    // TODO implement
    switch (jData.opcode)
    {
    case OP_JAL:
        regs[REG_RA] = progCounter + 8;
        //fall through
    case OP_J:
        progCounter = ((progCounter + 4) & 0xf0000000) | (addr << 2);
        break;
    }

    return -1;
}

// returns non zero on mem failure
int handleMem(IData &iData)
{
    uint32_t addr = iData.rsValue + iData.seImm;
    uint32_t data = 0;
    // TODO: perform operations through cache 
    // and overall clean this up
    switch (iData.opcode)
    {
    case OP_SB:
        return memStore->setMemValue(addr, iData.rtValue & 0xFF, BYTE_SIZE);
    case OP_SH:
        return memStore->setMemValue(addr, iData.rtValue & 0xFFFF, HALF_SIZE);
    case OP_SW:
        return memStore->setMemValue(addr, iData.rtValue, WORD_SIZE);
     case OP_LBU:
        ret = doLoad(addr, BYTE_SIZE, rt);
        break;
    case OP_LHU:
        ret = doLoad(addr, HALF_SIZE, rt);
        break;
    case OP_LW:
        ret = doLoad(addr, WORD_SIZE, rt);
        break;
    default:
        return 0;
    }
}