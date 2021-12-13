#include <iostream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <errno.h>
#include <math.h> 
#include "MemoryStore.h"
#include "RegisterInfo.h"
#include "EndianHelpers.h"
#include "DriverFunctions.h"

#include "cache_sim.h"

#define ADDRESS_LEN 32 

using std::vector;

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
    for (int i; i < numSets; i++) {
        for (int j; j < assoc; j++) {
            metaDataBits[i][j].valid = 0;
            metaDataBits[i][j].tag = 0;
            metaDataBits[i][j].dirty  = 0;
            metaDataBits[i][j].lru = 0;
        }
    }

    for (int i; i < numSets; i++) {
        for (int j; j < assoc; j++) {
            for(int k; k<blockSize; k++) {
                cacheData[i][j][k] = 0;
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
void Cache::getCacheValue(uint32_t address, uint32_t & value, MemEntrySize size, uint32_t cycle){
    bool miss;
    value = 0;
    value = UINT64_MAX;

    // look at each byte  
    for(int i; i< size; i++){
        uint32_t byteAddr = address+i;
        uint32_t byte;
        miss = getCacheByte(byteAddr, byte, cycle);
        if(i ==0){
            if(miss == 0) {
                hits++;
            } else {
                misses++;
            }
        }
        value = value | (byte << ((size-1-i)*8));
    }
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
    uint32_t latency = 0;
    bool miss;
    for (int i = 0; i < size; i++) {
        uint32_t byte = (value & (mask << ((size-1-i)*8))) >> ((size-1-i)*8);
        //uint32_t byte;
        miss = setCacheByte(address + i, byte, cycle);
        if(i ==0){
            if(miss == 0) {
                hits++;
            } else {
                misses++;
            }
        }
         if (miss) { 
            latency = missLatency;
        }
    }
    return latency;
}

int Cache::getCacheByte(uint32_t address, uint32_t & value, uint32_t cycle){
    uint32_t addressCopy = address;
    uint32_t addrTag = addressCopy >> (tagStart);
    uint32_t addressCopy = address;
    uint32_t addrIndex = (addressCopy << (tagEnd - indexEnd)) >> (ADDRESS_LEN - indexEnd - indexStart);
    uint32_t addressCopy = address;
    uint32_t blockOffset = addressCopy << (ADDRESS_LEN - offsetEnd) >> (ADDRESS_LEN - offsetEnd- offsetStart);

        // iterate through each block in a set
       for (int i; i< assoc; i++) {
        // read Hit
        if(metaDataBits[addrIndex][i].valid  && metaDataBits[addrIndex][i].tag == addrTag) {
            if (metaDataBits[addrIndex][i].cycleReady > cycle) return 1;
            value = cacheData[addrIndex][i][blockOffset];
            updateLRU(addrIndex, i);
            // helper function to keep track of LRU block for each set in cache
           // updateLRU();
            return 0;
        } 
    }
    // gets data from memory after a cache miss 
    int newBlock = cacheMiss(addressCopy, addrTag, addrIndex, blockOffset);
    value = cacheData[addrIndex][newBlock][blockOffset];
    metaDataBits[addrIndex][newBlock].cycleReady = cycle + missLatency;
    return 1;
}

int Cache::setCacheByte(uint32_t address, uint32_t value, uint32_t cycle) {
    uint32_t addressCopy = address;
    uint32_t addrTag = addressCopy >> (tagStart);
    addressCopy = address;
    uint32_t addrIndex = (addressCopy << (tagEnd - indexEnd)) >> (ADDRESS_LEN - indexEnd - indexStart);
    addressCopy = address;
    uint32_t blockOffset = addressCopy << (ADDRESS_LEN - offsetEnd) >> (ADDRESS_LEN - offsetEnd- offsetStart);

    // loop through blocks in the set, starting at startBlock
    for (int i = 0; i < assoc; i++) {
        if (metaDataBits[addrIndex][i].valid  && metaDataBits[addrIndex][i].tag == addrTag) { // WRITE HIT
            if (metaDataBits[addrIndex][i].cycleReady > cycle) {
                return 1; // we've hit before, but are emulating latency 
            }
            cacheData[addrIndex][assoc][blockOffset] = (uint8_t) value;
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
    return 1;
}

int Cache::cacheMiss(uint32_t address, uint32_t tag, uint32_t addrIndex, uint32_t blockOffset) {
    // 1. replace appropriate block based on lru
    // 2. check the dirty bit to see if we need to do write back)
    // 3. execute write back
    // 4. overwrite data in cache 
    
    uint32_t setBlock;
    // compare each block in a set to see which one is LRU
    if((metaDataBits[addrIndex][0].lru > metaDataBits[addrIndex][1].lru) || metaDataBits[addrIndex][1].valid != 1) {
        setBlock = 1;
    } else {
        setBlock = 0;
    }

    // check if dirty, if so then write-back
    if (metaDataBits[addrIndex][setBlock].dirty) {
        uint32_t memAddr = (metaDataBits[addrIndex][setBlock].tag << tagStart) | (addrIndex << indexStart);
        // is this correct?????
        // loop by each byte and set memory based on whats written in cache data 
        for(int byteOffset = 0; byteOffset < blockSize; byteOffset++){
            mainMem->setMemValue(memAddr + byteOffset, (uint32_t) cacheData[addrIndex][setBlock][byteOffset], BYTE_SIZE);
        }

        // mainMem->setMemValue(address, cacheData[addrIndex][setBlock][blockOffset], BYTE_SIZE);
    }
  
    uint32_t blockStartMemAddr = (address >> offsetEnd) << offsetEnd; // removing byte offset from address
    // loop by each byte read from memory and write it into cache to over write data
     for (int byteOffset = 0; byteOffset < blockSize; byteOffset++) {
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
    for(int i; i < assoc; i++) {
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
    for (int setNum = 0; setNum < numSets; setNum++) {
        for(int i =0; i< assoc; i++){
            if (metaDataBits[setNum][i].valid && metaDataBits[setNum][i].dirty) {
                uint32_t memAddr = (metaDataBits[setNum][i].tag << tagStart) | (setNum << indexStart);
                for (int byte_offset = 0; byte_offset < blockSize; byte_offset++) {
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

// Cache *createCache(CacheConfig config, MemoryStore *mem) {
//     return new Cache(config, mem);
// }

// void deleteCache(Cache *cache) {
//     delete cache;
// }
