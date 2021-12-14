#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <algorithm>
#include <vector>
#include <errno.h>
#include <math.h> 
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DriverFunctions.h"

// CACHE

#define ADDRESS_LEN 32 

using std::vector;

struct metaData {
    bool valid;
    bool dirty; 
    uint32_t tag;
    uint32_t lru;
    uint32_t cycleReady;
};

class Cache {
    private:
        //vector<uint32_t> tags;
        //vector<uint32_t> valid;
        // cacheBlock is uint8_t array
        // index is brings us to right block and block offset brings us to the right byte 
        vector<vector<vector<uint8_t>>> cacheData;
        // metadata unique block, struct of tag, valid , dirty bits
        // dimensions of 2d array is numSet x 2 blocks for 2 way
        // dimensions fof 2d array is just index and 1 block for direct map
        vector<vector<metaData>> metaDataBits;
        uint32_t hits;
        uint32_t misses;
        uint32_t address;
        CacheType cacheType;
        // write back cache 
        // dirty bit set everytime write into cache line 
        // if dirty bit is set + valid bit, throw out block and update block in memory 
        uint8_t lru;
        uint32_t numBlocks, numSets, blockSize, cacheSize, missLatency, assoc;
        int offsetStart, offsetEnd, indexStart, indexEnd, tagStart, tagEnd;
        // funcitons to set valid bit, lru, dirty, 
        int setCacheByte(uint32_t address, uint32_t value, uint32_t cycle);
        int getCacheByte(uint32_t address, uint32_t & value, uint32_t cycle);
        void updateLRU(int addrIndex, int recentlyUsed);
        MemoryStore *mainMem;
    public:
        Cache(CacheConfig &cache, MemoryStore *mem);
        int getCacheValue(uint32_t address, uint32_t & value, MemEntrySize size, uint32_t cycle);
        int setCacheValue(uint32_t address, uint32_t value, MemEntrySize size, uint32_t cycle);
        uint32_t getHits();
        uint32_t getMisses();
        uint32_t cacheMiss(uint32_t address, uint32_t tag, uint32_t addrIndex, uint32_t blockOffset);
        void drain();
        ~Cache();
};

// initialize once for I cache and D cache
Cache::Cache(CacheConfig &config, MemoryStore *mem) {
    hits = 0;
    misses = 0;
    blockSize = config.blockSize;
    cacheSize= config.cacheSize;
    missLatency = config.missLatency;
    cacheType = config.type;
    mainMem = mem;
    numBlocks = cacheSize/blockSize;
    if(cacheType == TWO_WAY_SET_ASSOC) {
        assoc = 2;
    } else {
        assoc = 1;
    }
    if(cacheType == TWO_WAY_SET_ASSOC) {
        numSets = numBlocks/2;
    } else {
        numSets = numBlocks;
    }
    for (uint32_t i = 0; i < numSets; i++) {
        metaDataBits.emplace_back();
        for (uint32_t j = 0; j < assoc; j++) {
            metaDataBits[i].emplace_back();
            // metaDataBits[i][j].tag = 0;
            // metaDataBits[i][j].dirty  = 0;
            // metaDataBits[i][j].lru = 0;
        }
    }

    for (uint32_t i = 0; i < numSets; i++) {
        cacheData.emplace_back();        
        for (uint32_t j = 0; j < assoc; j++) {
            cacheData[i].emplace_back();
            for (uint32_t k = 0; k<blockSize; k++) {
                cacheData[i][j].emplace_back();
            }
        }
    }


    offsetStart = 0;
    offsetEnd   = log2(blockSize);
    indexStart  = log2(blockSize);
    indexEnd    = indexStart + log2(numSets);
    tagStart    = indexStart + log2(numSets);
    tagEnd      = ADDRESS_LEN;
    
}

 // address given is the address of the first byte
int Cache::getCacheValue(uint32_t address, uint32_t & value, MemEntrySize size, uint32_t cycle){
    int result;
    value = 0;

    // look at each byte  
    for(uint32_t i = 0; i< size; i++){
        uint32_t byteAddr = address+i;
        uint32_t byte;
        result = getCacheByte(byteAddr, byte, cycle);
        if(i ==0){
            if(result == 0) {
                hits++;
            } else {
                misses++;
                hits--;
            }
        }
        value = value | (byte << ((size-1-i)*8));
    }
    return result;
}

    // cache read miss procedure:
    // check the dirty bit if its 1 we write back, if its 0 we dont write back
    // overwrite contents of cache block by grabbing data from memory
    // overwrite the tag in our metadata 
    // when replacing :  make valid bit 1,  dirty bit 0,  update lru 
    // make a helper function to run the cache miss procedure (use for both getCacheValue and setCacheValue)
    // write miss
    // set dirty to 1 every time you write the cache line
    
    // do we count the hits for each byte or for each word?
    // what should we be returning for the getCache function? the value of the word?
    

int Cache::setCacheValue(uint32_t address, uint32_t value, MemEntrySize size, uint32_t cycle) {
    uint32_t mask = 0xFF;
    int result;
    for (uint32_t i = 0; i < size; i++) {
        uint32_t byte = (value & (mask << ((size-1-i)*8))) >> ((size-1-i)*8);
        //uint32_t byte;
        result = setCacheByte(address + i, byte, cycle);
        if(i ==0){
            if(result == 0) {
                hits++;
            } else {
                misses++;
                hits--;
            }
        }
    }
    return result;
}

int Cache::getCacheByte(uint32_t address, uint32_t & value, uint32_t cycle){
    uint32_t addressCopy = address;
    uint32_t addrTag = addressCopy << (ADDRESS_LEN - tagEnd) >> (ADDRESS_LEN-tagEnd) >> (tagStart);
    addressCopy = address;
    uint32_t addrIndex = addressCopy << (ADDRESS_LEN - indexEnd) >> (ADDRESS_LEN - indexEnd) >> indexStart;
    addressCopy = address;
    uint32_t blockOffset = addressCopy << (ADDRESS_LEN - offsetEnd) >> (ADDRESS_LEN - offsetEnd) >> offsetStart;

        // iterate through each block in a set
       for (uint32_t i = 0; i< assoc; i++) {
        // read Hit
        if(metaDataBits[addrIndex][i].valid  && metaDataBits[addrIndex][i].tag == addrTag) {
            if (metaDataBits[addrIndex][i].cycleReady > cycle) return missLatency;
            value = cacheData[addrIndex][i][blockOffset];
            updateLRU(addrIndex, i);
            // helper function to keep track of LRU block for each set in cache
           // updateLRU();
            return 0;
        } 
    }
    // gets data from memory after a cache miss 
    uint32_t newBlock = cacheMiss(addressCopy, addrTag, addrIndex, blockOffset);
    value = cacheData[addrIndex][newBlock][blockOffset];
    metaDataBits[addrIndex][newBlock].cycleReady = cycle + missLatency;
    return missLatency;
}

int Cache::setCacheByte(uint32_t address, uint32_t value, uint32_t cycle) {
    uint32_t addressCopy = address;
    uint32_t addrTag = (ADDRESS_LEN - tagEnd) >> (ADDRESS_LEN-tagEnd) >> (tagStart);
    addressCopy = address;
    uint32_t addrIndex = (addressCopy << (ADDRESS_LEN - indexEnd)) >> (ADDRESS_LEN - indexEnd) >> indexStart;
    addressCopy = address;
    uint32_t blockOffset = addressCopy << (ADDRESS_LEN - offsetEnd) >> (ADDRESS_LEN - offsetEnd) >> offsetStart;

    // loop through blocks in the set, starting at startBlock
    for (uint32_t i = 0; i < assoc; i++) {
        if (metaDataBits[addrIndex][i].valid  && metaDataBits[addrIndex][i].tag == addrTag) { // WRITE HIT
            if (metaDataBits[addrIndex][i].cycleReady > cycle) {
                return missLatency; // we've hit before, but are emulating latency 
            }
            cacheData[addrIndex][i][blockOffset] = (uint8_t) value;
            metaDataBits[addrIndex][i].dirty = 1;
            updateLRU(addrIndex, i);
            return 0;
        }
    }

    // WRITE MISS
    int newBlock = cacheMiss(address, addrTag, addrIndex, blockOffset);
    cacheData[addrIndex][newBlock][blockOffset] = (uint8_t) value;
    metaDataBits[addrIndex][newBlock].dirty = 1;
    metaDataBits[addrIndex][newBlock].cycleReady = cycle + missLatency;
    return missLatency;
}

uint32_t Cache::cacheMiss(uint32_t address, uint32_t tag, uint32_t addrIndex, uint32_t blockOffset) {
    // 1. replace appropriate block based on lru
    // 2. check the dirty bit to see if we need to do write back)
    // 3. execute write back
    // 4. overwrite data in cache 
    
    uint32_t setBlock;
    // compare each block in a set to see which one is LRU
    if((metaDataBits[addrIndex][0].lru > metaDataBits[addrIndex][1].lru) || (cacheType == TWO_WAY_SET_ASSOC && !metaDataBits[addrIndex][1].valid)) {
        setBlock = 1;
    } else {
        setBlock = 0;
    }

    // check if dirty, if so then write-back
    if (metaDataBits[addrIndex][setBlock].dirty) {
        uint32_t memAddr = (metaDataBits[addrIndex][setBlock].tag << tagStart) | (addrIndex << indexStart);
        // is this correct?????
        // loop by each byte and set memory based on whats written in cache data 
        for(uint32_t byteOffset = 0; byteOffset < blockSize; byteOffset++){
            mainMem->setMemValue(memAddr + byteOffset, (uint32_t) cacheData[addrIndex][setBlock][byteOffset], BYTE_SIZE);
        }

        // mainMem->setMemValue(address, cacheData[addrIndex][setBlock][blockOffset], BYTE_SIZE);
    }
  
    uint32_t blockStartMemAddr = (address >> offsetEnd) << offsetEnd; // removing byte offset from address
    // loop by each byte read from memory and write it into cache to over write data
     for (uint32_t byteOffset = 0; byteOffset < blockSize; byteOffset++) {
        uint32_t temp;
        mainMem->getMemValue(blockStartMemAddr + byteOffset, temp, BYTE_SIZE);
        cacheData[addrIndex][setBlock][byteOffset] = (uint8_t) temp;
    }
    // cacheData[addrIndex][setBlock][blockOffset] = mainMem->getMemValue(address, value, BYTE_SIZE);
    metaDataBits[addrIndex][setBlock].dirty = 0;
    metaDataBits[addrIndex][setBlock].valid = 1;
    updateLRU(addrIndex, setBlock);
    metaDataBits[addrIndex][setBlock].tag = tag;
    return setBlock;
    
}

void Cache::updateLRU(int addrIndex, int recentlyUsed){
    for(uint32_t i = 0; i < assoc; i++) {
        if(metaDataBits[addrIndex][recentlyUsed].lru > metaDataBits[addrIndex][recentlyUsed].lru) {
            metaDataBits[addrIndex][i].lru -= 1;
        }
    }
    metaDataBits[addrIndex][recentlyUsed].lru = assoc - 1; 
}

uint32_t Cache::getHits() {
    return hits;
}

uint32_t Cache::getMisses() {
    return misses;
}

void Cache::drain() {
    for (uint32_t setNum = 0; setNum < numSets; setNum++) {
        for(uint32_t i = 0; i< assoc; i++){
            if (metaDataBits[setNum][i].valid && metaDataBits[setNum][i].dirty) {
                uint32_t memAddr = (metaDataBits[setNum][i].tag << tagStart) | (setNum << indexStart);
                for (uint32_t byte_offset = 0; byte_offset < blockSize; byte_offset++) {
                    mainMem->setMemValue(memAddr + byte_offset, (uint32_t) cacheData[setNum][i][byte_offset], BYTE_SIZE);
                }   
            }
        }
    }
}

Cache::~Cache(){
    metaDataBits.clear();
    cacheData.clear();   
}

// SIMULATOR

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
    J,
    E // for illegal excepetion
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
        case E:
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
        case E:
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
    if (instr == 0xfeedfeed)
        return R;
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
    default:
        return E;
    }
}

// Arg: current instruction
// Return: struct RData holding relevant register instruction data
struct RData getRData(uint32_t instr)
{
    if (instr == 0xfeedfeed)
        return RData{};
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

enum CycleStatus
{
    NOT_HALTED,
    HALTED
};

Cache *icache;
Cache *dcache;
PipeState pipeState;
uint32_t pc;
MemoryStore *memStore;
IFID ifid;
IDEX idex;
EXMEM exmem;
MEMWB memwb;
bool haltSeen;
int memHaltCycles;
uint32_t lastPcFetch;
uint32_t lastInstructionFetch;
CycleStatus cycleStatus{};
SimulationStats simStats{};

int initSimulator(CacheConfig &icConfig, CacheConfig &dcConfig, MemoryStore *mainMem)
{
    icache = new Cache{icConfig, mainMem};
    dcache = new Cache{dcConfig, mainMem};

    pipeState = PipeState{};
    pc = 0;
    memStore = mainMem;
    ifid = IFID{};
    idex = IDEX{};
    exmem = EXMEM{};
    memwb = MEMWB{};
    haltSeen = false;
    memHaltCycles = 0;
    lastPcFetch = UINT32_MAX;
    lastInstructionFetch = 0;
    cycleStatus = CycleStatus{};
    simStats = SimulationStats{};
    return 0;
}

uint8_t getSign(uint32_t value)
{
    return (value >> 31) & 0x1;
}

// returns true if overflow occured, false otherwise
// writes the result to result if no overflow occured
bool doAddSub(uint32_t s1, uint32_t s2, bool isAdd, bool checkOverflow, uint64_t &result)
{
    bool overflow = false;

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
        return true;
    }

    //Otherwise update state and return success.
    result = static_cast<uint32_t>(result);

    return false;
}

// sets rdValue to new value of rd, or UINT64_MAX if none
// returns true if instruction caused exception, false otherwise
bool handleRInstEx(RData &rData, uint64_t &rdValue)
{
    switch (rData.funct)
    {
    case FUN_ADD:
        return doAddSub(rData.rsValue, rData.rtValue, true, true, rdValue);
    case FUN_ADDU:
        return doAddSub(rData.rsValue, rData.rtValue, true, false, rdValue);
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
        return doAddSub(rData.rsValue, rData.rtValue, false, true, rdValue);
    case FUN_SUBU:
        return doAddSub(rData.rsValue, rData.rtValue, false, false, rdValue);
    default:
        // nextPc = EXCEPTION_ADDR; ?
        cerr << "Illegal function code at address "
             << "0x" << hex
             << setfill('0') << setw(8) << pc - 4 << ": " << (uint16_t)rData.funct << endl;
        exit(1);
        break;
    }

    return false;
}

// sets rtValue to new value of rt destination, or UINT64_MAX if no new value
// returns true if overflow detected, false otherwise
bool handleImmInstEx(IData &iData, uint64_t &rtValue)
{
    rtValue = UINT64_MAX;

    switch (iData.opcode)
    {
    case OP_ADDI:
        return doAddSub(iData.rsValue, iData.seImm, true, true, rtValue);
    case OP_ADDIU:
        return doAddSub(iData.rsValue, iData.seImm, true, false, rtValue);
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
    return false;
}

// returns true when stall, false otherwise
int handleMem(EXMEM &exmem)
{
    IData &iData = exmem.instructionData.data.iData;
    uint32_t addr = iData.rsValue + iData.seImm;
    uint32_t data = 0;
    
    int delay = 0;
    switch (iData.opcode)
    {
    case OP_SB:
        return dcache->setCacheValue(addr, iData.rtValue, BYTE_SIZE, pipeState.cycle);
    case OP_SH:
        return dcache->setCacheValue(addr, iData.rtValue, HALF_SIZE, pipeState.cycle);
    case OP_SW:
        return dcache->setCacheValue(addr, iData.rtValue, WORD_SIZE, pipeState.cycle);
    case OP_LBU:
        if (delay = dcache->getCacheValue(addr, data, BYTE_SIZE, pipeState.cycle))
        {
            return delay;
        }
        else
            exmem.regWriteValue = data;
        break;
    case OP_LHU:
        if (delay = dcache->getCacheValue(addr, data, HALF_SIZE, pipeState.cycle))
        {

            return delay;
        }
        else
            exmem.regWriteValue = data;
        break;
    case OP_LW:
        if (delay = dcache->getCacheValue(addr, data, WORD_SIZE, pipeState.cycle))
        {
            return delay;
        }
        else
            exmem.regWriteValue = data;
        break;
    }
    return 0;
}

bool branchNeedsStall(InstructionData &currentInstr, IDEX &nextInstr, EXMEM &nextNextInstr, bool checkRt)
{
    auto rs = currentInstr.rs();
    auto rt = currentInstr.rt();

    if (rs == nextNextInstr.regToWrite && nextNextInstr.regToWrite != 0 && nextNextInstr.instructionData.isMemRead())
    {
        return true;
    }

    if (checkRt && rt == nextNextInstr.regToWrite && nextNextInstr.regToWrite != 0 && nextNextInstr.instructionData.isMemRead())
    {
        return true;
    }

    if (rs == nextInstr.regToWrite && nextInstr.regToWrite != 0)
    {
        return true;
    }

    if (checkRt && rt == nextInstr.regToWrite && nextInstr.regToWrite != 0)
    {
        return true;
    }

    return false;
}

void handleBranchForwarding(InstructionData &instr, EXMEM &exmem)
{
    if (instr.rs() == exmem.regToWrite && exmem.regToWrite != 0 && exmem.regWriteValue != UINT64_MAX)
    {
        instr.rsValue(exmem.regWriteValue);
    }
    else if (instr.rt() == exmem.regToWrite && exmem.regToWrite != 0 && exmem.regWriteValue != UINT64_MAX)
    {
        instr.rtValue(exmem.regWriteValue);
    }
}

void handleMemForwarding(InstructionData &instr, MEMWB &memwb)
{
    if (instr.rt() == memwb.regToWrite && memwb.regToWrite != 0 && memwb.regWriteValue != UINT64_MAX)
    {
        instr.rtValue(memwb.regWriteValue);
    }
}

bool isFuncCodeValid(uint8_t funct)
{
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
        return true;
    default:
        return false;
    }
}

CycleStatus runCycle()
{
    IFID nextIfid{};
    IDEX nextIdex{};
    EXMEM nextExmem{};
    MEMWB nextMemwb{};

    bool stallIf = false;
    bool stallId = false;
    bool stallMem = false;

    // if simulated cache miss time is not over yet
    if (--memHaltCycles > 0) {
        pipeState.cycle++;
        simStats.totalCycles++;
        return cycleStatus;
    }
    else memHaltCycles = 0;

    // writeBack
    // first as we're emulating writing to register file
    // happening before reading to it
    if (memwb.regWriteValue != UINT64_MAX && memwb.regToWrite != 0)
    {
        regs[memwb.regToWrite] = memwb.regWriteValue;
    }

    // instructionFetch
    uint32_t instruction = 0;

    // if something else stalls the pipeline, we rerun the instruction fetch stage
    // however, that results in getting a cache value again that should be stored in the pipeline instead
    // this avoids that by maintaining a "cache" for the last fetched instruction that won't increment icache hits
    if (lastPcFetch == pc) {
        instruction = lastInstructionFetch;
    }

    else if (!haltSeen)
    {
        auto delay = icache->getCacheValue(pc, instruction, MemEntrySize::WORD_SIZE, pipeState.cycle);
        if (delay)
        {
            // cache miss, halt
            stallIf = true;
            memHaltCycles = std::max(memHaltCycles, delay);
        } else {
            lastPcFetch = pc;
            lastInstructionFetch = instruction;
        }
    }

    nextIfid.pc = pc;
    uint32_t nextPc = pc + 4;
    nextIfid.instruction = instruction;
    if (instruction == 0xfeedfeed)
        haltSeen = true;

    // instructionDecode
    nextIdex.instructionData.tag = getInstType(ifid.instruction);
    switch (nextIdex.instructionData.tag)
    {
    case R:
    {
        nextIdex.instructionData.data.rData = getRData(ifid.instruction);

        // Illegal instruction exception check
        if (!isFuncCodeValid(nextIdex.instructionData.data.rData.funct))
        {
            nextPc = EXCEPTION_ADDR;
            nextIfid.instruction = 0;
            haltSeen = false;
            nextIdex.instructionData = InstructionData{};
            break;
        }
        if (nextIdex.instructionData.data.rData.funct == FUN_JR)
        {
            handleBranchForwarding(nextIdex.instructionData, exmem);
            nextPc = nextIdex.instructionData.data.rData.rsValue;
            stallId = branchNeedsStall(nextIdex.instructionData, idex, exmem, false);
        }
        else
        {
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
                nextPc = ifid.pc + 4 + ((static_cast<int32_t>(iData.seImm)) << 2);
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
    case E:
        nextPc = EXCEPTION_ADDR;
        nextIfid.instruction = 0; // squash instruction after illegal instruction exception
        haltSeen = false;
        nextIdex = IDEX{};
    }
    if (nextIdex.instructionData.tag != E)
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

    bool exOverflow = false;

    switch (idex.instructionData.tag)
    {
    case R:
        exOverflow = handleRInstEx(idex.instructionData.data.rData, nextExmem.regWriteValue);
        break;
    case I:
        exOverflow = handleImmInstEx(idex.instructionData.data.iData, nextExmem.regWriteValue);
        break;
    default:
    {
        break;
    }
    }

    if (exOverflow)
    {
        nextPc = EXCEPTION_ADDR;
        nextIfid.instruction = 0;
        nextIdex = IDEX{};
        nextExmem = EXMEM{};
        haltSeen = false;
    }

    // mem
    if (exmem.instructionData.tag == I)
    {
        handleMemForwarding(exmem.instructionData, memwb);
        auto delay = handleMem(exmem);
        if (delay) {
            memHaltCycles = std::max(memHaltCycles, delay);
            stallMem = true;
        }
    }

    nextMemwb = exmem;

    // writeback trigger halt
    if (memwb.instruction == 0xfeedfeed)
        cycleStatus = HALTED;


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
    if (!stallIf && !stallId && !stallMem)
    {
        ifid = nextIfid;
        pc = nextPc;
    }

    if (stallIf)
    {
        // insert bubble
        ifid = IFID{};
    }

    if (!stallId && !stallMem)
    {
        idex = nextIdex;
    }
    else if (stallId && !stallMem)
    {
        // insert bubble
        idex = IDEX{};
    }

    if (!stallMem)
    {
        exmem = nextExmem;
        memwb = nextMemwb;
    }
    else
    {
        // insert bubble
        memwb = MEMWB{};
    }

    return cycleStatus;
}

int runCycles(unsigned int cycles)
{
    CycleStatus cycleStatus{};
    for (; cycles > 0 && cycleStatus != HALTED; cycles--)
    {
        cycleStatus = runCycle();
    }
    pipeState.cycle--;
    dumpPipeState(pipeState);
    pipeState.cycle++;
    return cycleStatus == HALTED;
}
int runTillHalt()
{
    CycleStatus cycleStatus{};
    do
    {
        cycleStatus = runCycle();
    } while (cycleStatus != HALTED);
    pipeState.cycle--;
    dumpPipeState(pipeState);
    pipeState.cycle++;
    return 0;
}
int finalizeSimulator()
{
    // Set the register values in the struct for printing...
    SimulationStats s;
    s.totalCycles = pipeState.cycle;
    s.icHits = icache->getHits();
    s.icMisses = icache->getMisses();
    s.dcHits = dcache->getHits();
    s.dcMisses = dcache->getMisses();
    printSimStats(s);

    icache->drain();
    dcache->drain();

    delete icache;
    delete dcache;

    RegisterInfo reg;
    memset(&reg, 0, sizeof(RegisterInfo));
    fillRegisterState(reg);

    dumpRegisterState(reg);
    dumpMemoryState(memStore);

    return 0;
}