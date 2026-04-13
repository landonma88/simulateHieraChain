#include <queue>
#include <string>
#include <mutex>
#include <map>
#include <iostream>
#include "common.h"

using namespace std;

#ifndef SHARD_H
#define SHARD_H

extern std::mutex global_performance_mtx;

// 叶子分片
class Shard{

public:
    int txId = 0;
    int shardId;
    int orderingCapacity; // 交易排序能力（每秒能够处理的交易处理）
    int executionCapacity; // 交易执行能力（美秒能够执行的工作量）
    int batchFetchSize; // 分片单位时间内(500ms)从交易池拉取交易数量
    int transactionSendRate; // 客户端交易发送速率

    // 用于统计交易吞吐和延迟的变量
    double committedTxCount = 0; // 1s内提交的交易数量
    double committedSubTxCount = 0; // 1s内提交的交易数量(跨片交易按1笔交易算)
    double totalLatency = 0;

    std::vector<string> accessControlList; // 状态权限目录
    std::queue<transaction> transactionMempool; // 交易池

private:
    std::mutex mempoolMutex; // 交易池读写互斥锁
    std::mutex performance_mtx; // 当前分片的交易吞吐和延迟性能读写锁

public:
    Shard(int _shardid, int _order_capability, int _execution_capability, int _processBatch, int _tx_sendRate, vector<string> permission_list); // 初始化函数
    
    void generateTransactions(vector<transaction>& txs); // 生成交易
    void enqueueTransactions(); // 向交易池添加一批新来的交易
    void enqueueRemoteTransactions(vector<transaction>& txs); // 向交易池添加一份
    void fetchTransactions(); // 从交易池取走一部分交易、最多processBatch个
    void runConsensus(vector<transaction>& txs); // 对拿到的交易进行共识
    void executeTransactions(vector<transaction>& txs); // 执行共识晚的一批交易
    void printPerformanceStats();
    void startMetrics(); // 计算分片当前的交易吞吐和延迟
    void start(); // 启动分片
    double getCurrentTimestamp();
};


class CoordinatorShard{

    public:

        int txId = 0;
        int shardId;
        int transactionSendRate; // 每秒收到的客户端总交易数量
        int batchFetchSize;
        int orderingCapacity;

        std::queue<transaction> transactionMempool; // 交易池
        vector<pair<pair<int, int>, double>> involvedShardIds; // 该分片收到的所有跨片交易占的比例

        std::mutex mempoolMutex; // 交易池锁

    public:
        CoordinatorShard(int _shardid, int _transactionSendRate, int _batchFetchSize, int _order_capability, vector<pair<pair<int, int>, double>> _involvedShardIds){
            shardId = _shardid;
            transactionSendRate = _transactionSendRate;
            batchFetchSize = _batchFetchSize;
            orderingCapacity = _order_capability;
            involvedShardIds = _involvedShardIds;
        }

        void generateTransactions(vector<transaction>& txs); // 生成交易
        void enqueueTransactions(); // 向交易池添加交易
        void fetchTransactions(thread_safety_map<int, Shard*>& shards); // 从交易池
        void runConsensus(vector<transaction>& txs, thread_safety_map<int, Shard*>& shards);
        void start(thread_safety_map<int, Shard*>& shards);
        double getCurrentTimestamp();
};

#endif // SHARD_H