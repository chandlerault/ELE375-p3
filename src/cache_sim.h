#include <vector>

using std::vector;


struct metaData {
    int valid;
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