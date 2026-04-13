#include <thread>
#include <functional>
#include <chrono>
#include <iomanip>
#include <ctime>
#include "shard.h"
#include <stdlib.h>
#include <time.h>

using namespace std;

#ifndef SHARD_CPP
#define SHARD_CPP

void CoordinatorShard::start(thread_safety_map<int, Shard*>& shards){

    // 向交易池添加交易
    std::thread addTransactions_thread([this] {
        enqueueTransactions();
    });

    // 从交易池拉取交易
    std::thread removeTransactions_thread([this, &shards] {
        fetchTransactions(shards);
    });

    // // 计算当前分片的 TPS
    // std::thread monitor_thread([this] {
    //     monitor();
    // });

    addTransactions_thread.detach();
    removeTransactions_thread.detach();
    // monitor_thread.detach();
}


double CoordinatorShard::getCurrentTimestamp(){
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();    
    double second = std::chrono::duration<double>(epoch).count();
    return second;
}

void CoordinatorShard::generateTransactions(vector<transaction> &txs){

    // cout << "一次性生成" << txs_persec << "笔交易至交易池" << endl;
    double second = getCurrentTimestamp();
    for(int i = 0; i < transactionSendRate; i++){ // 每次生成 txs_persec 笔跨片交易
        transaction tx;
        tx.type = 2;
        string prefix_txid = to_string(shardId) + to_string(txId);
        tx.txId = prefix_txid;
        tx.sendedTime = second;
        txs.push_back(tx);
        txId++;
    }
}

void CoordinatorShard::enqueueTransactions(){

    int addThread = 80000;

    while(true){

        vector<transaction> txs;
        generateTransactions(txs);

        mempoolMutex.lock(); // 手动加锁

        int tx_size = txs.size();
        for(int i = 0; i < tx_size; i++){
            transactionMempool.push(txs.at(i));
        }

        mempoolMutex.unlock(); // 手动加锁

        int remaining_per = double(addThread - tx_size) / addThread * 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(remaining_per)); // 模拟共识过程
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 模拟客户端发送速度
    }
}

void CoordinatorShard::fetchTransactions(thread_safety_map<int, Shard*>& shards){

    while (true)
    {

        int real_batchFetchSize = 0;

        mempoolMutex.lock(); // 手动加锁

        if(!transactionMempool.empty()){

            vector<transaction> txs;
            int remaining_size = transactionMempool.size(); // 交易池中剩下的交易数

            if(remaining_size >= batchFetchSize){
                real_batchFetchSize = batchFetchSize;
            }
            else{
                real_batchFetchSize = remaining_size;
            }

            for(int i = 0; i < real_batchFetchSize; i++){
                txs.push_back(transactionMempool.front());
                transactionMempool.pop();
            }

            // 开始对交易进行共识和执行
            runConsensus(txs, shards);
        }

        mempoolMutex.unlock(); // 手动释放锁

        int remaining_per = double(orderingCapacity - real_batchFetchSize) / orderingCapacity * 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(remaining_per)); // 模拟共识过程
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 模拟共识过程
    }
}

void CoordinatorShard::runConsensus(vector<transaction>& txs, thread_safety_map<int, Shard*>& shards){
    int tx_size = txs.size();

    // 将共识成功的交易发送至相应分片交易池
    // 遍历 destination_pairs
    int pair_size = involvedShardIds.size();
    for(int i = 0; i < pair_size; i++){
        auto item = involvedShardIds.at(i);
        auto shardids_pair = item.first;
        auto percentage = item.second;

        int subtx_size = percentage * tx_size; // 这一批所有的跨片交易数量

        vector<transaction> subtxs;
        for(int i = 0; i < subtx_size; i++){
            subtxs.push_back(txs.at(i));
        }

        // 将subtxs中的交易分别压到 shardids_pair 中不同的分片中
        int shardid1 = shardids_pair.first;
        int shardid2 = shardids_pair.second;

        auto shard = shards.find(shardid1);
        shard->enqueueRemoteTransactions(subtxs);

        shard = shards.find(shardid2);
        shard->enqueueRemoteTransactions(subtxs);
    }
}


void Shard::generateTransactions(vector<transaction>& txs){

    double second = getCurrentTimestamp();
    for(int i = 0; i < transactionSendRate; i++){
        transaction tx;
        tx.type = 1;

        string prefix_txid = to_string(shardId) + to_string(txId);
        tx.txId = prefix_txid;

        tx.sendedTime = second;

        txs.push_back(tx);
        txId++;
    }
}

void Shard::enqueueTransactions(){

    int addThread = 80000;

    while (true)
    {
        vector<transaction> txs;
        generateTransactions(txs);

        mempoolMutex.lock(); // 手动加锁
        int tx_size = txs.size();
        for(int i = 0; i < tx_size; i++){
            transactionMempool.push(txs.at(i));
        }
        mempoolMutex.unlock(); // 手动加锁

        int remaining_per = double(addThread - tx_size) / addThread * 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(remaining_per)); // 模拟共识过程
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 模拟客户端发送速度
    }
}

void Shard::enqueueRemoteTransactions(vector<transaction>& txs){

    mempoolMutex.lock(); // 手动加锁

    int tx_size = txs.size();
    for(int i = 0; i < tx_size; i++){
        transactionMempool.push(txs.at(i));
    }

    mempoolMutex.unlock(); // 手动加锁
}

void Shard::fetchTransactions(){
    while (true)
    {
        mempoolMutex.lock(); // 手动加锁

        int remove_size = 0;

        if(!transactionMempool.empty()){

            performance_mtx.lock();  // 加锁

            double time = getCurrentTimestamp();
            vector<transaction> txs;
            int remaining_size = transactionMempool.size(); // 交易池中剩下的交易数

            if(remaining_size >= batchFetchSize){
                remove_size = batchFetchSize;
            }
            else{
                remove_size = remaining_size;
            }

            // int execution_workload = 0;
            for(int i = 0; i < remove_size; i++){

                auto tx = transactionMempool.front();
                txs.push_back(tx);
                transactionMempool.pop();

                if(tx.type == 1){
                    // execution_workload += 1;
                    committedTxCount += 1;
                }
                else if(tx.type == 2){
                    // execution_workload += 1;
                    committedTxCount += 0.5;
                }

                double latency = time - tx.sendedTime;
                totalLatency += latency;
            }

            committedSubTxCount += remove_size;

            performance_mtx.unlock(); // 解锁

            // 调换顺序
            runConsensus(txs);
            // executionTxs(txs);
        }

        mempoolMutex.unlock(); // 手动释放锁

        int remaining_per = double(orderingCapacity - remove_size) / orderingCapacity * 1000;
        std::this_thread::sleep_for(std::chrono::milliseconds(remaining_per)); // 模拟共识过程
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 模拟共识过程
    }
}

void Shard::runConsensus(vector<transaction>& txs){
    int tx_size = txs.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // std::this_thread::sleep_for(std::chrono::milliseconds(int((float(tx_size) / order_capability) * 500)));
}

void Shard::executeTransactions(vector<transaction>& txs){

    std::lock_guard<std::mutex> performance_lock(performance_mtx);

    // 暂时假设系统中全部是片内交易
    int tx_size = txs.size();
    int execution_workload = 0;

    for(int i = 0; i < tx_size; i++){ // 交易的总执行负载
        if(txs.at(i).type == 1){
            execution_workload += 1;
            committedTxCount += 1;
        }
        else if(txs.at(i).type == 2) {
            execution_workload += 1;
            committedTxCount += 0.5;
        }
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(int((float(execution_workload) / execution_capability) * 500)));

    committedSubTxCount += tx_size;

    double time = getCurrentTimestamp();
    for(int i = 0; i < tx_size; i++){
        double latency = time - txs.at(i).sendedTime;
        totalLatency += latency;
    }
}

void Shard::start(){

    // 向交易池添加交易
    std::thread addTransactions_thread([this] {
        enqueueTransactions();
    });

    // 从交易池拉取交易
    std::thread removeTransactions_thread([this] {
        fetchTransactions();

    });

    // 计算当前分片的TPS
    std::thread monitor_thread([this] {
        startMetrics();
    });

    addTransactions_thread.detach();
    removeTransactions_thread.detach();
    monitor_thread.detach();

    cout << "启动分片 " << shardId << endl;
}

double Shard::getCurrentTimestamp(){

    // 获取当前系统时间
    auto now = std::chrono::system_clock::now();
    
    // 获取当前时间距离 Unix 时间戳的持续时间（包括小数部分）
    auto epoch = now.time_since_epoch();
    
    // 转换为浮动类型，秒数可以包含小数
    double second = std::chrono::duration<double>(epoch).count();

    return second;
}

void Shard::printPerformanceStats(){

    std::lock_guard<std::mutex> performance_lock(performance_mtx);

    if(committedTxCount == 0){
        // cout << "分片"<< shardid << "交易延迟 = 0" << endl;
        // cout << "分片"<< shardid << "交易吞吐 = 0" << endl;
    }
    else{
        throughputs.at(shardId) = committedTxCount;
        auto latency = make_pair(committedSubTxCount, totalLatency);
        latencys.at(shardId) = latency;
        // cout << "分片" << shardid << "交易吞吐 = "<< committedTxCount << ", 交易延迟 = "<< total_latency / committedTxCount << endl;
        committedTxCount = 0;
        committedSubTxCount = 0;
        totalLatency = 0;
    }
}

void Shard::startMetrics(){ // 统计分片当前的交易吞吐和延迟

    while (true)
    {
        printPerformanceStats();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 每秒统计一次吞吐和延迟
    }
}

Shard::Shard(int _shardid, int _orderingCapacity, int _executionCapacity, int _batchFetchSize, int _transactionSendRate, vector<string> _accessControlList):
shardId(_shardid), orderingCapacity(_orderingCapacity), executionCapacity(_executionCapacity), batchFetchSize(_batchFetchSize), transactionSendRate(_transactionSendRate) {
    accessControlList = _accessControlList;
}

#endif // SHARD_CPP