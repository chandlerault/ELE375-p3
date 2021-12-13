#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DriverFunctions.h"

#include "cache_sim.h"

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

/* // Cache values stored 
enum CACHE_VALUE {
   ICACHE;
   DCACHE;
}*/

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
    J //,
    //E // for illegal excepetion
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
    uint64_t regWriteValue = UINT64_MAX;
    uint8_t regToWrite;
};

using EXMEM = IDEX;

using MEMWB = EXMEM;

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
    case OP_BGTZ:
    case OP_BLEZ:
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
    //default:
    // return E;
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
        (uint8_t)(instr & 0x3f)          // uint8_t funct
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

enum CycleStatus {
    NOT_HALTED,
    HALTED
};

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
CycleStatus cycleStatus{};
SimulationStats simStats{};

int initSimulator(CacheConfig &icConfig, CacheConfig &dcConfig, MemoryStore *mainMem)
{
    icache = Cache{icConfig, mainMem};
    //createCache(icConfig, mainMem);
    dcache = Cache{dcConfig, mainMem};
    //createCache(dcConfig, mainMem);
    pipeState = PipeState{};
    pc = 0;
    memStore = mainMem;
    ifid = IFID{};
    idex = IDEX{};
    exmem = EXMEM{};
    memwb = MEMWB{};
    haltSeen = false;
    cycleStatus = CycleStatus{};
    simStats = SimulationStats{};
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
        rdValue = rData.rtValue >> rData.shamt;
        break;
    case FUN_SUB:
    case FUN_SUBU:
        rdValue = rData.rsValue - rData.rtValue;
        break;
    default:
        // nextPc = EXCEPTION_ADDR; ?
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

// returns true when stall, false otherwise
bool handleMem(EXMEM &exmem)
{
    IData &iData = exmem.instructionData.data.iData;
    uint32_t addr = iData.rsValue + iData.seImm;
    uint32_t data = 0;
    // TODO: perform operations through cache
    // and overall clean this up
    switch (iData.opcode)
    {
    case OP_SB:
        if (dcache.setCacheValue(addr, iData.rtValue, BYTE_SIZE, pipeState.cycle))
        {
            return true;
        }
        break;
    case OP_SH:
        if (dcache.setCacheValue(addr, iData.rtValue, HALF_SIZE, pipeState.cycle))
        {
            return true;
        }
        break;
    case OP_SW:
        if (dcache.setCacheValue(addr, iData.rtValue, WORD_SIZE, pipeState.cycle))
        {
            return true;
        }
        break;
    case OP_LBU:
        dcache.getCacheValue(addr, data, BYTE_SIZE, pipeState.cycle);
        if (data == UINT64_MAX)
        {
            return true;
        }
        else
            exmem.regWriteValue = data;
        break;
    case OP_LHU:
        dcache.getCacheValue(addr, data, HALF_SIZE, pipeState.cycle);
        if (data == UINT64_MAX)
        {

            return true;
        }
        else
            exmem.regWriteValue = data;
        break;
    case OP_LW:
        dcache.getCacheValue(addr, data, WORD_SIZE, pipeState.cycle);
        if (data == UINT64_MAX)
        {
            return true;
        }
        else
            exmem.regWriteValue = data;
        break;
    }
    return 0;
}

bool branchNeedsStall(InstructionData &currentInstr, IDEX &nextInstr, EXMEM &nextNextInstr, bool checkRt) {
    auto rs = currentInstr.rs();
    auto rt = currentInstr.rt();

    if(rs == nextNextInstr.regToWrite && nextNextInstr.regToWrite != 0 && nextNextInstr.instructionData.isMemRead()) {
        return true;
    }

    if(checkRt && rt == nextNextInstr.regToWrite && nextNextInstr.regToWrite != 0 && nextNextInstr.instructionData.isMemRead()) {
        return true;
    }
    
    if (rs == nextInstr.regToWrite && nextInstr.regToWrite != 0) {
        return true;
    }

    if (checkRt && rt == nextInstr.regToWrite && nextInstr.regToWrite != 0) {
        return true;
    }

    return false;
}

void handleBranchForwarding(InstructionData &instr, EXMEM &exmem) {
    if (instr.rs() == exmem.regToWrite && exmem.regToWrite != 0 && exmem.regWriteValue != UINT64_MAX) {
        instr.rsValue(exmem.regWriteValue);
    }
    else if (instr.rt() == exmem.regToWrite && exmem.regToWrite != 0 && exmem.regWriteValue != UINT64_MAX) {
        instr.rtValue(exmem.regWriteValue);
    }
}

void handleMemForwarding(InstructionData &instr, MEMWB &memwb) {
    if (instr.rt() == memwb.regToWrite && memwb.regToWrite != 0 && memwb.regWriteValue != UINT64_MAX) {
        instr.rtValue(memwb.regWriteValue);
    }
}

/*
int checkFunction(uint8_t funct){
    switch (funct)
    {
    case FUN_ADD:
    case FUN_ADDU:
    case FUN_AND:
    case FUN_JR:
    case FUN_NOR:
    case FUN_OR:
    case FUN_SLT:
    case FUN_SLTU:
    case FUN_SLL:
    case FUN_SRL:
    case FUN_SUB:
    case FUN_SUBU:
        return 0;
    default:
        return 1;
    }
}
*/


CycleStatus runCycle()
{
    IFID nextIfid{};
    IDEX nextIdex{};
    EXMEM nextExmem{};
    MEMWB nextMemwb{};

    bool stallIf = false;
    bool stallId = false;
    bool stallMem = false;

    // writeBack
    // first as we're emulating writing to register file 
    // happening before reading to it 
    if (memwb.regWriteValue != UINT64_MAX && memwb.regToWrite != 0)
    {
        regs[memwb.regToWrite] = memwb.regWriteValue;
    }

    // instructionFetch
    uint32_t instruction;
    icache.getCacheValue(pc, instruction, MemEntrySize::WORD_SIZE, pipeState.cycle);
    instruction = haltSeen ? 0 : instruction;
    if (instruction == UINT64_MAX)
    {
        stallIf = true;
    }
    nextIfid.pc = pc;
    uint32_t nextPc = pc + 4;
    nextIfid.instruction = instruction;
    if (instruction == 0xfeedfeed) haltSeen = true;

    // instructionDecode
    nextIdex.instructionData.tag = getInstType(ifid.instruction);
    switch (nextIdex.instructionData.tag)
    {
    case R:
    {
        nextIdex.instructionData.data.rData = getRData(ifid.instruction);
        /* // Illegal instruction exception check
        if (checkFunction(nextIdex.instructionData.data.rData.funct)){
            // todo should this be nextPc
            nextPc = EXCEPTION_ADDR;
            nextIfid.instruction = 0;
            break;
        }
        */
        if (nextIdex.instructionData.data.rData.funct == FUN_JR) {
            handleBranchForwarding(nextIdex.instructionData, exmem);
            nextPc = nextIdex.instructionData.data.rData.rsValue;
            stallId = branchNeedsStall(nextIdex.instructionData, idex, exmem, false);
        } else {
            nextIdex.regToWrite = nextIdex.instructionData.data.rData.rd;
        }
        break;
    }
    case I:
    {
        nextIdex.instructionData.data.iData = getIData(ifid.instruction);
        auto &iData = nextIdex.instructionData.data.iData;
        switch (iData.opcode)
        {
        case OP_BEQ:
            handleBranchForwarding(nextIdex.instructionData, exmem);
            if (iData.rsValue == iData.rtValue)
            {
                nextPc = ifid.pc + 4 +((static_cast<int32_t>(iData.seImm)) << 2);
            }
            stallId = branchNeedsStall(nextIdex.instructionData, idex, exmem, true);
            break;
        case OP_BNE:
            handleBranchForwarding(nextIdex.instructionData, exmem);
            if (iData.rsValue != iData.rtValue)
            {
                nextPc = ifid.pc + 4 + ((static_cast<int32_t>(iData.seImm)) << 2);
            }
            stallId = branchNeedsStall(nextIdex.instructionData, idex, exmem, true);
            break;
        case OP_BGTZ:
            handleBranchForwarding(nextIdex.instructionData, exmem);
            if (iData.rsValue > 0)
            {
                nextPc = ifid.pc + 4 + ((static_cast<int32_t>(iData.seImm)) << 2);
            }
            stallId = branchNeedsStall(nextIdex.instructionData, idex, exmem, false);
            break;
        case OP_BLEZ:
            handleBranchForwarding(nextIdex.instructionData, exmem);
            if (iData.rsValue <= 0)
            {
                nextPc = ifid.pc + 4 + ((static_cast<int32_t>(iData.seImm)) << 2);
            }
            stallId = branchNeedsStall(nextIdex.instructionData, idex, exmem, false);
            break;
        case OP_SB:
        case OP_SH:
        case OP_SW:
            break;
        default:
            nextIdex.regToWrite = iData.rt;
            break;
        }
        break;
    }
    case J:
    {
        auto jData = getJData(ifid.instruction, ifid.pc);
        switch (jData.opcode)
        {
        case OP_JAL:
            nextIdex.regToWrite = 31;
            nextIdex.regWriteValue = ifid.pc + 8;
            // fallthrough
        case OP_J:
            nextPc = ((ifid.pc + 4) & 0xf0000000) | (jData.addr << 2);
            break;
        }
        nextIdex.instructionData.data.jData = jData;
        break;
    }
    /*
    case E:
        nextPc = EXCEPTION_ADDR;
        nextifid.instruction = 0; // squash instruction after illegal instruction exception
    */
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
    }

    // execute

    // forwarding of results from register data being written back
    if (memwb.regWriteValue != UINT64_MAX && memwb.regToWrite != 0)
    { // if (MEM/WB.RegWrite and (MEM/WB.RegisterRd ≠ 0)
        if (memwb.regToWrite == idex.instructionData.rs())
        { // (and MEM/WB.registerRd = ID/EX.registerRs)) ForwardA = 01
            idex.instructionData.rsValue(memwb.regWriteValue);
        }
        if (memwb.regToWrite == idex.instructionData.rt())
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
        if (exmem.regToWrite == idex.instructionData.rt())
        { // (and EX/WB.registerRd = ID/EX.registerRt)) ForwardB = 01
            idex.instructionData.rtValue(exmem.regWriteValue);
        }
    }

    nextExmem = idex;

    switch (idex.instructionData.tag)
    {
    case R:
        nextExmem.regWriteValue = handleRInstEx(idex.instructionData.data.rData);
        break;
    case I:
        nextExmem.regWriteValue = handleImmInstEx(idex.instructionData.data.iData);
        break;
    case J:
    {
        break;
    }
    }

    // mem
    if (exmem.instructionData.tag == I) {
        handleMemForwarding(exmem.instructionData, memwb);
        if (handleMem(exmem)) stallMem = true;
    }

    nextMemwb = exmem;

    
    // writeback trigger halt
    if (memwb.instruction == 0xfeedfeed) cycleStatus = HALTED;

     // update pipe state information
    pipeState.cycle++;
    pipeState.ifInstr = nextIfid.instruction;
    pipeState.idInstr = nextIdex.instruction;
    pipeState.exInstr = nextExmem.instruction;
    pipeState.memInstr = nextMemwb.instruction;
    pipeState.wbInstr = memwb.instruction;

    // update total cycles
    simStats.totalCycles++;

    // finish cycle
    if (!stallIf && !stallId && !stallMem) {
        ifid = nextIfid;
        pc = nextPc;
    }

    if (!stallId && !stallMem) {
        idex = nextIdex;
    } else if (stallId && !stallMem) {
        // insert bubble
        idex = IDEX{};
    }

    if (!stallMem) {
        exmem = nextExmem;
        memwb = nextMemwb;
    }

    return cycleStatus;
}

int runCycles(unsigned int cycles) {
    CycleStatus cycleStatus{};
    for(; cycles > 0 && cycleStatus != HALTED; cycles--) {
        cycleStatus = runCycle();
    }
    pipeState.cycle--;
    dumpPipeState(pipeState);
    pipeState.cycle++;
    return cycleStatus == HALTED;
}
int runTillHalt() {
    CycleStatus cycleStatus{};
    do {
        cycleStatus = runCycle();
    } while(cycleStatus != HALTED);
    pipeState.cycle--;
    dumpPipeState(pipeState);
    pipeState.cycle++;
    return 0;
}          
int finalizeSimulator() {
    //Set the register values in the struct for printing...
    // SimulationStats s;
    // s.icHits = ic->getHits();
    // s.icMisses = ic->getMisses();
    // s.dcHits = dc->getHits();
    // s.dcMisses = dc->getMisses();
    // printSimStats(s)

    // ic->drain();
    // dc->drain();

    // deleteCache(ic);
    // deleteCache(dc);

    RegisterInfo reg;
    memset(&reg, 0, sizeof(RegisterInfo));
    fillRegisterState(reg);

    dumpRegisterState(reg);
    dumpMemoryState(memStore);

    return 0;
}  