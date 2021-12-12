#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DriverFunctions.h"

#define EXCEPTION_ADDR 0x8000
using namespace std;
//TODO: Fix the error messages to output the correct PC in case of errors.

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

//Static global variables...
static uint32_t regs[NUM_REGS];

// uint8_t getSign(uint32_t value)
// {
//     return (value >> 31) & 0x1;
// }

// int doAddSub(uint8_t rd, uint32_t s1, uint32_t s2, bool isAdd, bool checkOverflow)
// {
//     bool overflow = false;
//     int32_t result = 0;

//     if (isAdd)
//     {
//         result = static_cast<int32_t>(s1) + static_cast<int32_t>(s2);
//     }
//     else
//     {
//         result = static_cast<int32_t>(s1) - static_cast<int32_t>(s2);
//     }

//     if (checkOverflow)
//     {
//         if (isAdd)
//         {
//             overflow = getSign(s1) == getSign(s2) && getSign(s2) != getSign(result);
//         }
//         else
//         {
//             overflow = getSign(s1) != getSign(s2) && getSign(s2) == getSign(result);
//         }
//     }

//     if (overflow)
//     {
//         //Inform the caller that overflow occurred so it can take appropriate action.
//         return OVERFLOW;
//     }

//     //Otherwise update state and return success.
//     regs[rd] = static_cast<uint32_t>(result);

//     return 0;
// }

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

    uint8_t rs()
    {
        switch (this->tag)
        {
        case R:
            return this->data.rData.rs;
        case I:
            return this->data.iData.rs;
        default:
            return 0;
        }
    }

    uint8_t rt()
    {
        switch (this->tag)
        {
        case R:
            return this->data.rData.rt;
        case I:
            return this->data.iData.rt;
        default:
            return 0;
        }
    }

    void rsValue(uint64_t val)
    {
        switch (this->tag)
        {
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

    void rtValue(uint64_t val)
    {
        switch (this->tag)
        {
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

    bool isMemRead()
    {
        if (this->tag == I)
        {
            switch (this->data.iData.opcode)
            {
            case OP_LBU:
            case OP_LHU:
            case OP_LW:
                return true;
            }
        }
        return false;
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

// returns true if value ready, false otherwise
bool setCacheValue(Cache *cache, MemoryStore *mem, uint64_t cycle, uint32_t addr, MemEntrySize size, uint32_t value)
{
    int ret;
    switch (size)
    {
    case BYTE_SIZE:
        ret = mem->setMemValue(addr, value & 0xFF, BYTE_SIZE);
        break;
    case HALF_SIZE:
        ret = mem->setMemValue(addr, value & 0xFFFF, HALF_SIZE);
        break;
    case WORD_SIZE:
        ret = mem->setMemValue(addr, value, WORD_SIZE);
        break;
    default:
        cerr << "Unknown mem write word size provided.\n";
        exit(1);
    }
    if (ret)
    {
        cerr << "Failed to write to memory address 0x" << std::hex << std::setw(8) << std::setfill('0') << addr << endl;
    }
    return true;
}

// get opcode from instruction
uint8_t getOpcode(uint32_t instr)
{
    return (instr >> 26) & 0x3f;
}

// Arg: current opcode
// Return: enum INST_TYPE tag associated with instruction being executed
enum INST_TYPE getInstType(uint32_t instr)
{
    if (instr == 0xfeedfeed) return R;
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
    // TODO: notify caller somehow, for illegal arg exception
    std::cerr << "Unknown exception encountered" << endl;
    exit(1);
}

// Arg: current instruction
// Return: struct RData holding relevant register instruction data
struct RData getRData(uint32_t instr)
{
    if (instr == 0xfeedfeed) return RData{};
    uint8_t rs = (instr >> 21) & 0x1f;
    uint8_t rt = (instr >> 16) & 0x1f;
    struct RData rData = {
        getOpcode(instr), // uint8_t opcode;
        regs[rs],         // uint32_t rsValue;
        regs[rt],         // uint32_t rtValue;
        rs,
        rt,
        (uint8_t)((instr >> 11) & 0x1f), // uint8_t rd
        (uint8_t)((instr >> 6) & 0x1f),  // uint8_t shamt
        (uint8_t)(instr & 0x3f)          // uint8_tfunct
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
struct JData getJData(uint32_t instr, uint32_t pc)
{
    struct JData jData = {
        getOpcode(instr),  // uint8_t opcode;
        instr & 0x3ffffff, // uint32_t addr;
        pc                 // uint32_t oldPC;
    };

    return jData;
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
bool haltSeen;

int initSimulator(CacheConfig &icConfig, CacheConfig &dcConfig, MemoryStore *mainMem)
{
    icache = Cache{};
    dcache = Cache{};
    pipeState = PipeState{};
    pc = 0;
    memStore = mainMem;
    ifid = IFID{};
    idex = IDEX{};
    exmem = EXMEM{};
    memwb = MEMWB{};
    haltSeen = false;
    return 0;
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
        cerr << "Illegal function code at address "
             << "0x" << hex
             << setfill('0') << setw(8) << pc - 4 << ": " << (uint16_t)rData.funct << endl;
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

void handleJInstEx(JData &jData)
{
    // TODO: is this needed? what to do for jal
    return;
}

// returns non zero when stall
int handleMem(IData &iData)
{
    uint32_t addr = iData.rsValue + iData.seImm;
    uint32_t data = 0;
    // TODO: perform operations through cache
    // and overall clean this up
    switch (iData.opcode)
    {
    case OP_SB:
        if (setCacheValue(&dcache, memStore, pipeState.cycle, addr, BYTE_SIZE, iData.rtValue))
        {
            // TODO: stall
        }
        break;
    case OP_SH:
        if (setCacheValue(&dcache, memStore, pipeState.cycle, addr, HALF_SIZE, iData.rtValue))
        {
            // TODO: stall
        }
        break;
    case OP_SW:
        if (setCacheValue(&dcache, memStore, pipeState.cycle, addr, WORD_SIZE, iData.rtValue))
        {
            // TODO: stall
        }
        break;
    case OP_LBU:
        data = getCacheValue(&dcache, memStore, pipeState.cycle, addr, BYTE_SIZE);
        if (data == UINT64_MAX)
        {
            // TODO stall
        }
        else
            iData.rtValue = data;
        break;
    case OP_LHU:
        data = getCacheValue(&dcache, memStore, pipeState.cycle, addr, HALF_SIZE);
        if (data == UINT64_MAX)
        {
            // TODO stall
        }
        else
            iData.rtValue = data;
        break;
    case OP_LW:
        data = getCacheValue(&dcache, memStore, pipeState.cycle, addr, WORD_SIZE);
        if (data == UINT64_MAX)
        {
            // TODO stall
        }
        else
            iData.rtValue = data;
        break;
    }
    return 0;
}

enum CycleStatus {
    NOT_HALTED,
    HALTED
};

CycleStatus runCycle()
{
    CycleStatus cycleStatus{};
    IFID nextIfid{};
    IDEX nextIdex{};
    EXMEM nextExmem{};
    MEMWB nextMemwb{};

    bool stallIf = false;
    bool stallId = false;

    // instructionFetch
    auto instruction = haltSeen ? 0 : getCacheValue(&icache, memStore, pipeState.cycle, pc, MemEntrySize::WORD_SIZE);
    if (instruction == UINT64_MAX)
    {
        stallIf = true;
    }
    nextIfid.pc = pc;
    pc += 4;
    nextIfid.instruction = instruction;
    if (instruction == 0xfeedfeed) haltSeen = true;

    // instructionDecode
    nextIdex.instructionData.tag = getInstType(ifid.instruction);
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
                pc = ifid.pc + 4 +((static_cast<int32_t>(iData.seImm)) << 2);
                break;
            }
            break;
        case OP_BNE:
            if (iData.rsValue != iData.rtValue)
            {
                pc = ifid.pc + 4 + ((static_cast<int32_t>(iData.seImm)) << 2);
                break;
            }
            break;
        case OP_BGTZ:
            if (iData.rsValue > 0)
            {
                pc = ifid.pc + 4 + ((static_cast<int32_t>(iData.seImm)) << 2);
                break;
            }
            break;
        case OP_BLEZ:
            if (iData.rsValue <= 0)
            {
                pc = ifid.pc + 4 + ((static_cast<int32_t>(iData.seImm)) << 2);
                break;
            }
            break;
        default:
            break;
        }
        nextIdex.instructionData.data.iData = iData;
        break;
    }
    case J:
    {
        auto jData = getJData(ifid.instruction, ifid.pc);
        switch (jData.opcode)
        {
        case OP_JAL:
            // TODO: what to do, WB is weird
        case OP_J:
            pc = ((ifid.pc + 4) & 0xf0000000) | (jData.addr << 2);
            break;
        }
        nextIdex.instructionData.data.jData = jData;
        break;
    }
    }
    nextIdex.instruction = ifid.instruction;

    // if (ID/EX.MemRead and
    //  ((ID/EX.RegisterRt = IF/ID.RegisterRs) or
    //  (ID/EX.RegisterRt = IF/ID.RegisterRt)))
    // we need to wait for the memory fetch to succeed
    auto idexRt = idex.instructionData.rt();
    if (idex.instructionData.isMemRead() && idexRt != 0 && (idexRt == nextIdex.instructionData.rs() || idexRt == nextIdex.instructionData.rt()))
    {
        stallId = true;
        // insert bubble into pipeline
        nextIdex = IDEX{};
    }

    // execute

    // forwarding of results from register data being written back
    if (memwb.regWriteValue != UINT64_MAX && memwb.regToWrite != 0)
    { // if (MEM/WB.RegWrite and (MEM/WB.RegisterRd ≠ 0)
        if (memwb.regToWrite == idex.instructionData.rs())
        { // (and MEM/WB.registerRd = ID/EX.registerRs)) ForwardA = 01
            idex.instructionData.rsValue(memwb.regWriteValue);
        }
        if (memwb.regToWrite == memwb.instructionData.rt())
        { // (and MEM/WB.registerRd = ID/EX.registerRt)) ForwardB = 01
            idex.instructionData.rtValue(memwb.regWriteValue);
        }
    }

    // forwarding of results from previous cycle's execute
    if (exmem.regWriteValue != UINT64_MAX && exmem.regToWrite != 0)
    { // if (EX/WB.RegWrite and (EX/WB.RegisterRd ≠ 0)
        if (exmem.regToWrite == idex.instructionData.rs())
        { // (and EX/WB.registerRd = ID/EX.registerRs)) ForwardA = 01
            idex.instructionData.rsValue(exmem.regWriteValue);
        }
        if (exmem.regToWrite == exmem.instructionData.rt())
        { // (and EX/WB.registerRd = ID/EX.registerRt)) ForwardB = 01
            idex.instructionData.rtValue(exmem.regWriteValue);
        }
    }

    switch (idex.instructionData.tag)
    {
    case R:
        nextExmem.regWriteValue = handleRInstEx(idex.instructionData.data.rData);
        nextExmem.regToWrite = idex.instructionData.data.rData.rd;
        break;
    case I:
        nextExmem.regWriteValue = handleImmInstEx(idex.instructionData.data.iData);
        nextExmem.regToWrite = idex.instructionData.data.iData.rt;
        break;
    case J:
    {
        handleJInstEx(idex.instructionData.data.jData);
        nextExmem.regWriteValue = UINT64_MAX;
        nextExmem.regToWrite = 0;
        break;
    }
    }
    nextExmem.instruction = idex.instruction;
    nextExmem.instructionData = idex.instructionData;

    // mem
    if (exmem.instructionData.tag == I) {
        // TODO: handle stalls
        handleMem(exmem.instructionData.data.iData);
    }

    nextMemwb = exmem;

    // writeBack
    if (memwb.regWriteValue != UINT64_MAX && memwb.regToWrite != 0)
    {
        regs[memwb.regToWrite] = memwb.regWriteValue;
    }

    if (memwb.instruction == 0xfeedfeed) cycleStatus = HALTED;

     // update pipe state information
    pipeState.cycle++;
    pipeState.ifInstr = nextIfid.instruction;
    pipeState.idInstr = nextIdex.instruction;
    pipeState.exInstr = nextExmem.instruction;
    pipeState.memInstr = nextMemwb.instruction;
    pipeState.wbInstr = memwb.instruction;

    // finish cycle
    if (stallIf || stallId) {
        pc = ifid.pc;
    } else {
        ifid = nextIfid;
    }
    
    idex = nextIdex;

    exmem = nextExmem;
    memwb = nextMemwb;

    return cycleStatus;
}

int runCycles(unsigned int cycles) {
    CycleStatus cycleStatus{};
    for(; cycles > 0 && cycleStatus != HALTED; cycles--) {
        cycleStatus = runCycle();
    }
    dumpPipeState(pipeState);
    return cycleStatus == HALTED;
}
int runTillHalt() {
    CycleStatus cycleStatus{};
    do {
        cycleStatus = runCycle();
    } while(cycleStatus != HALTED);
    dumpPipeState(pipeState);
    return 0;
}          
int finalizeSimulator() {
    //Set the register values in the struct for printing...
    RegisterInfo reg;
    memset(&reg, 0, sizeof(RegisterInfo));
    fillRegisterState(reg);

    dumpRegisterState(reg);
    dumpMemoryState(memStore);

    return 0;
}  