#include <thread>
#include <functional>
#include <chrono>
#include <iomanip>
#include <ctime>
#include "shard.h"
#include <stdlib.h>
#include <time.h>
#include <sstream>
#include <fstream>
#include <set>
#include <regex>
#include <algorithm>

using namespace std;

#ifndef SHARD_CPP
#define SHARD_CPP

void Shard::printWorkload(){

// 使用 lambda 简化重复逻辑
    auto display = [](const std::string& label, const std::map<int, txsDistribution>& data) {
        std::cout << "--- " << label << " ---" << std::endl;
        
        // C++11 基于范围的 for 循环
        for (const std::pair<const int, txsDistribution>& item : data) {
            // item.first 是 map 的 key
            // item.second 是 txsDistribution 结构体
            const int& id = item.first;
            const txsDistribution& dist = item.second;

            std::cout << "[ID: " << id << "] "
                      << "Type: " << (dist.type == 1 ? "Intra" : "Cross") << " | "
                      << "Count: " << dist.txCount << " | "
                      << "Involved Shards: [";
            
            // 打印 vector
            for (size_t i = 0; i < dist.invlovedShardIds.size(); ++i) {
                std::cout << dist.invlovedShardIds[i] 
                          << (i == dist.invlovedShardIds.size() - 1 ? "" : ", ");
            }
            std::cout << "]" << std::endl;
        }
        
        if (data.empty()) {
            std::cout << "(Empty)" << std::endl;
        }
        std::cout << std::endl;
    };

    display("Intra-Shard Txs Distribution", intraShardTxsDistribution);
    display("Cross-Shard Txs Distribution", crossShardTxsDistribution);
}

void Shard::parseWorkload(){

    std::vector<string> workloadDistribution;

    std::vector<std::string> lines;
    std::ifstream file(workLoadDir);
    
    if (!file.is_open()) {
        std::cerr << "无法打开文件1: " << workLoadDir << std::endl;
        exit(1);
    }

    std::string line;
    while (std::getline(file, line)) {
        // 去除行首尾空白
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty()) continue;
        
        // 如果一行中有多个配置（逗号分隔），可以进一步分割
        std::istringstream iss(line);
        std::string item;
        
        // 如果你想将每个逗号分隔的部分作为独立元素
        while (std::getline(iss, item, ',')) {
            // 去除每个item前后的空格
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            
            if (!item.empty()) {
                workloadDistribution.push_back(item);
            }
        }
    }

    file.close();

    for(auto item: workloadDistribution){
        cout << item << endl;
    }

    // 从 workloadDistribution 中解析得到 map<int, txsDistribution> intraShardTxsDistribution 和 map<int, txsDistribution> crossShardTxsDistribution

    // 正则表达式说明：
    // (shard(\d+)) : 匹配第一个分片及其数字 ID
    // (_inner|_shard(\d+)) : 匹配 "_inner" 或者 "_shard" 及其后面的第二个 ID
    // :(\d+) : 匹配冒号后的交易数量
    
    std::regex txDisPattern; 
    txDisPattern = std::regex("shard(\\d+)(?:_inner|_shard(\\d+)):(\\d+)");

    std::smatch match;

    for (const std::string& entry : workloadDistribution) {
        if (std::regex_search(entry, match, txDisPattern)) {
            int shardId1 = std::stoi(match[1].str());
            int txCount = std::stoi(match[match.size() - 1].str());

            // 检查是片内还是跨片
            if (entry.find("inner") != std::string::npos) {
                // --- 片内交易 (Intra-shard) ---
                txsDistribution dist;
                dist.type = 1;
                dist.txCount = txCount;
                dist.invlovedShardIds = {shardId1};

                // Key 为分片 ID
                intraShardTxsDistribution[shardId1] = dist;

            } else {
                // --- 跨片交易 (Cross-shard) ---
                int shardId2 = std::stoi(match[2].str());
                
                txsDistribution dist;
                dist.type = 2;
                dist.txCount = txCount;
                dist.invlovedShardIds = {shardId1, shardId2};

                // Key 为两个分片的最近公共祖先 (LCA)
                int lcaKey = findLCA(shardId1, shardId2);
                
                // 如果找到 LCA，则放入跨片字典
                if (lcaKey != -1) {
                    crossShardTxsDistribution[lcaKey] = dist;
                } else {
                    std::cerr << "Warning: No LCA found for " << entry << std::endl;
                }
            }
        }
    }
    cout << "解析负载完成..." << endl;
}

int Shard::findLCA(int shardA, int shardB){

    if (shardA == shardB) return shardA;
    std::set<int> pathA;
    
    // 1. 记录 shardA 向上到根节点的所有路径
    int curr = shardA;
    pathA.insert(curr);
    while (parentMap.count(curr)) {
        curr = parentMap[curr];
        pathA.insert(curr);
    }

    // 2. 从 shardB 开始向上追溯，第一个在 pathA 中出现的节点就是 LCA
    curr = shardB;
    while (true) {
        if (pathA.count(curr)) {
            return curr;
        }
        if (parentMap.count(curr)) {
            curr = parentMap[curr];
        } else {
            break; // 到达根节点仍未找到
        }
    }
    return -1; // 没有公共祖先
}

void Shard::printShardTopology(){

    std::cout << "\n========== Shard Topology Report ==========" << std::endl;
    std::cout << std::left << std::setw(15) << "Ancestor ID" << " | " << "Child Shard IDs" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;

    for (const auto& [ancestorId, children] : topologyMap) {
        std::cout << std::left << std::setw(15) << ancestorId << " | [ ";
        
        for (size_t i = 0; i < children.size(); ++i) {
            std::cout << children[i] << (i == children.size() - 1 ? "" : ", ");
        }
        
        std::cout << " ]" << std::endl;
    }
    std::cout << "===========================================\n" << std::endl;
}

void Shard::parseTopology(){

    std::ifstream configFile(shardsTopologyDir);

    // 检查文件是否成功打开
    if (!configFile.is_open()) {
        std::cerr << "无法打开分片拓扑文件: " << shardsTopologyDir << std::endl;
        exit(1);
    }

    std::string line;
    while (std::getline(configFile, line)) {
        if (line.empty()) continue;

        // 寻找叶子分片
        if (line.find("leaf:") != std::string::npos) {
            size_t start = line.find('{');
            size_t end = line.find('}');
            
            if (start != std::string::npos && end != std::string::npos) {
                // 提取 "1,2"
                std::string idsStr = line.substr(start + 1, end - start - 1);
                std::stringstream ss(idsStr);
                std::string segment;
                
                // 以逗号分割提取每个ID
                while (std::getline(ss, segment, ',')) {
                    if (!segment.empty()) {
                        int leafId = std::stoi(segment);

                        cout << "leafId = " << leafId << endl;
                        cout << "this->shardId = " << this->shardId << endl;

                        // 在你的分片管理容器中寻找该 ID 并设置角色
                        if (leafId == this->shardId) {
                            ShardRole role(ShardRole::LEAF);
                            this->role = role;
                        }
                    }
                }
            }
            continue;
        }

        size_t commaPos = line.find(',');
        if (commaPos == std::string::npos) continue;

        // 解析当前行所属的祖先 ID
        int ancestorId = std::stoi(line.substr(0, commaPos));

        size_t start = line.find('[');
        size_t end = line.find(']');
        if (start != std::string::npos && end != std::string::npos) {
            std::string childrenStr = line.substr(start + 1, end - start - 1);
            std::stringstream ss(childrenStr);
            std::string childIdStr;

            while (std::getline(ss, childIdStr, ',')) {
                if (!childIdStr.empty()) {
                    int childId = std::stoi(childIdStr);
                    topologyMap[ancestorId].push_back(childId);
                    // 核心：记录每个子分片的直接父节点
                    parentMap[childId] = ancestorId;
                }
            }
        }
    }

    configFile.close();

    if (this->role == ShardRole::LEAF) {
        cout << "当前分片[" << this->shardId << "] 角色判定：叶子分片" << endl;
    } else {
        cout << "当前分片[" << this->shardId << "] 角色判定：协调者分片" << endl;
    }
    cout << "解析拓扑完成..." << endl;
}

// 提取分片Id
int Shard::parseShardId(){

    int shardId;
    std::ifstream file(shardIdDir);
    if (!file.is_open()) {
        std::cerr << "无法打开文件！" << std::endl;
        exit(1);
    }

    std::string line;
    if (std::getline(file, line)) {
        // 1. 找到 '=' 的位置
        size_t pos = line.find('=');
        
        if (pos != std::string::npos) {
            // 2. 截取 '=' 之后的部分
            std::string valueStr = line.substr(pos + 1);

            // 3. 转换为整数 (stoi 会自动处理前导空格)
            try {
                shardId = std::stoi(valueStr);
            } catch (const std::exception& e) {
                std::cerr << "Failed to convert shardId number: " << e.what() << std::endl;
            }
        }
    }

    file.close();
    return shardId;
}

void Shard::parseAccessControlList(){

    std::ifstream file(accessControlListDir);
    if (!file.is_open()) {
        std::cerr << "无法打开状态权限目录文件: " << accessControlListDir << std::endl;
        exit(1);
    }

    allAccessControlLists.clear();
    accessControlList.clear();

    std::regex shardHeaderPattern("^shard(\\d+):$");
    std::smatch match;
    int currentShardId = -1;

    std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        if (line.empty()) {
            continue;
        }

        if (std::regex_match(line, match, shardHeaderPattern)) {
            currentShardId = std::stoi(match[1].str());
            if (!allAccessControlLists.count(currentShardId)) {
                allAccessControlLists[currentShardId] = std::vector<std::string>();
            }
            continue;
        }

        if (currentShardId != -1) {
            allAccessControlLists[currentShardId].push_back(line);
        }
    }

    file.close();

    if (allAccessControlLists.count(shardId)) {
        accessControlList = allAccessControlLists[shardId];
    }
}

void Shard::generateTransactions(vector<transaction>& txs){

    txs.clear();
    if (transactionSendRate <= 0) {
        return;
    }

    auto pickRandomAccount = [&](int fromShardId) -> std::string {
        const std::vector<std::string>* accounts = nullptr;

        if (fromShardId == shardId && !accessControlList.empty()) {
            accounts = &accessControlList;
        } else if (allAccessControlLists.count(fromShardId) && !allAccessControlLists[fromShardId].empty()) {
            accounts = &allAccessControlLists[fromShardId];
        } else if (!accessControlList.empty()) {
            accounts = &accessControlList;
        }

        if (accounts != nullptr && !accounts->empty()) {
            int idx = rand() % accounts->size();
            return accounts->at(idx);
        }

        return "shard" + to_string(fromShardId) + "_account";
    };

    int intraLoad = 0;
    std::map<int, txsDistribution>::iterator itIntra = intraShardTxsDistribution.find(shardId);
    if (itIntra != intraShardTxsDistribution.end()) {
        intraLoad = itIntra->second.txCount;
    }

    std::vector<txsDistribution> myCrossDists;
    int crossLoad = 0;
    for (std::map<int, txsDistribution>::iterator it = crossShardTxsDistribution.begin(); it != crossShardTxsDistribution.end(); ++it) {
        const txsDistribution& dist = it->second;
        if (dist.invlovedShardIds.empty()) {
            continue;
        }

        bool currentInvolved = std::find(dist.invlovedShardIds.begin(), dist.invlovedShardIds.end(), shardId) != dist.invlovedShardIds.end();
        bool iAmGenerator = dist.invlovedShardIds[0] == shardId;

        if (currentInvolved && iAmGenerator) {
            myCrossDists.push_back(dist);
            crossLoad += dist.txCount;
        }
    }

    int totalLoad = intraLoad + crossLoad;
    if (totalLoad <= 0) {
        return;
    }

    int intraCount = int((long long)transactionSendRate * intraLoad / totalLoad);
    if (intraCount < 0) {
        intraCount = 0;
    } else if (intraCount > transactionSendRate) {
        intraCount = transactionSendRate;
    }
    int crossCount = transactionSendRate - intraCount;
    double second = getCurrentTimestamp();

    for (int i = 0; i < intraCount; i++) {
        transaction tx;
        tx.type = 1;
        tx.txId = to_string(shardId) + "_" + to_string(txId++);
        tx.invlovedShardIds.push_back(shardId);
        tx.RWSet.push_back(pickRandomAccount(shardId));
        tx.RWSet.push_back(pickRandomAccount(shardId));
        tx.sendedTime = second;
        txs.push_back(tx);
    }

    int remainingCrossCount = crossCount;
    int remainingCrossLoad = crossLoad;
    int crossSize = myCrossDists.size();
    for (int i = 0; i < crossSize && remainingCrossCount > 0; i++) {
        const txsDistribution& dist = myCrossDists[i];
        int allocCount = 0;
        if (i == crossSize - 1 || remainingCrossLoad <= 0) {
            allocCount = remainingCrossCount;
        } else {
            allocCount = int((long long)remainingCrossCount * dist.txCount / remainingCrossLoad);
        }

        if (allocCount < 0) {
            allocCount = 0;
        } else if (allocCount > remainingCrossCount) {
            allocCount = remainingCrossCount;
        }

        remainingCrossCount -= allocCount;
        remainingCrossLoad -= dist.txCount;

        for (int j = 0; j < allocCount; j++) {
            transaction tx;
            tx.type = 2;
            tx.txId = to_string(shardId) + "_" + to_string(txId++);
            tx.invlovedShardIds = dist.invlovedShardIds;

            for (size_t k = 0; k < dist.invlovedShardIds.size(); k++) {
                tx.RWSet.push_back(pickRandomAccount(dist.invlovedShardIds[k]));
            }

            if (tx.RWSet.empty()) {
                tx.RWSet.push_back(pickRandomAccount(shardId));
            }

            tx.sendedTime = second;
            txs.push_back(tx);
        }
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

    cout << "启动分片" << this->shardId << " ..."<< endl;
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

    // if(committedTxCount == 0){
    //     // cout << "分片"<< shardid << "交易延迟 = 0" << endl;
    //     // cout << "分片"<< shardid << "交易吞吐 = 0" << endl;
    // }
    // else{
    //     throughputs.at(shardId) = committedTxCount;
    //     auto latency = make_pair(committedSubTxCount, totalLatency);
    //     latencys.at(shardId) = latency;
    //     cout << "分片" << shardid << "交易吞吐 = "<< committedTxCount << ", 交易延迟 = "<< total_latency / committedTxCount << endl;
    //     committedTxCount = 0;
    //     committedSubTxCount = 0;
    //     totalLatency = 0;
    // }
}

void Shard::startMetrics(){ // 统计分片当前的交易吞吐和延迟

    while (true)
    {
        printPerformanceStats();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 每秒统计一次吞吐和延迟
    }
}

Shard::Shard() {

    this->orderingCapacity = ::orderingCapacity;
    this->executionCapacity = ::executionCapacity;
    this->batchFetchSize = ::batchFetchSize;
    this->transactionSendRate = ::transactionSendRate;

    // 获取分片id
    this->shardId = parseShardId();
    srand(static_cast<unsigned int>(time(NULL) + shardId));
    parseAccessControlList(); // 解析状态权限目录

    parseTopology(); // 解析系统拓扑
    printShardTopology(); // 打印系统拓扑结构
    parseWorkload(); // 解析负载
    printWorkload();
}

#endif // SHARD_CPP
