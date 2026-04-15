#ifndef SHARD_HELPER_H
#define SHARD_HELPER_H

#include <string>

class Shard;

class ShardHelper {
public:
    explicit ShardHelper(Shard& shard);

    double getCurrentTimestamp();
    int parseShardId();
    void parseTopology();
    void printShardTopology();
    int findLCA(int shardA, int shardB);
    void parseWorkload();
    void printWorkload();
    void parseOwnedStateIds();
    void printOwnedStateIds();
    int lookupShardByState(std::string stateId);

private:
    Shard& shard;
};

#endif // SHARD_HELPER_H
