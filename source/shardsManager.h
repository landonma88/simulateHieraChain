#ifndef SHARDSMANAGER_H
#define SHARDSMANAGER_H

#include "common.h"
#include "shard.h"
#include <sstream>

class ShardsManager{

private:
    std::map<int, std::vector<int>> topologyMap; // 存储 祖先 -> 子分片 的映射
    std::map<int, int> parentMap; // 存储 子分片 -> 父分片 的反向映射，用于向上追溯

    map<int, txsDistribution> intraShardTxsDistribution; // 片内交易负载
    map<int, txsDistribution> crossShardTxsDistribution; // 跨片交易负载 

public:
    thread_safety_map <int, Shard*> shards;

    ShardsManager(int leafShardCount, int CoordinatorShardCount, string& accessControlListDir);
    void startAllShards();

    void parseTopology();
    void printShardTopology();
    int findLCA(int shardA, int shardB);

    void parseWorkload();
    void printWorkload();
};







#endif // SHARDSMANAGER_H