#include <iostream>
#include <stdlib.h>
#include <thread>
#include <vector>
#include <regex>
#include <set>
#include <chrono>

#include "shard.h"
#include "shardsManager.h"

using namespace std;

// 全局变量
map<int, int> throughputs; // 每个分片的吞吐
map<int, pair<int, double>> latencys; // 每个分片的所有交易延迟

int orderingCapacity = 5000;
int executionCapacity = 8000;
int batchFetchSize = 5000;
int transactionSendRate = 2000;
string accessControlListDir = "/Users/tanghaibo_office/Desktop/Second Work Code/third_work/simulate_system/accessControlList";
string shardsTopologyDir = "/Users/tanghaibo_office/Desktop/Second Work Code/third_work/simulate_system/shardsTopology";
string WorkLoadDir = "/Users/tanghaibo_office/Desktop/Second Work Code/third_work/simulate_system/workloadProfile";

class parentChildrenids{
    public:
        int parentid = -1;
        vector<int> childrenids;
};

struct Cross_workload {
    int lca = -1;
    std::vector<int> shardNums; // "shard" 后面的数字集合
    int txnum;                // 冒号后面的数字
};

class topology_analyzer{

    public:
        vector<int> cooshardids; // 所有排序者的分片id
        vector<int> leafshardids;
        map<int, vector<int>> ParentChildMaps; //网络拓扑结构
        vector<Cross_workload> Cross_workloads; // 跨片负载
        map<int, int> cooshard_cstNums; // 上层分片的排序总量
        map<int, parentChildrenids> topology_relationship; // shardid -> parent & Child

    public:
        topology_analyzer(vector<int> _leafshardids){ // 初始化函数
            leafshardids = _leafshardids;
        }

        void extract_workloadLines(vector<string> workloadLines){
            for(auto line : workloadLines){
                if (line.find("inner") == std::string::npos) {
                    Cross_workload data;
                    std::regex shardRegex("shard(\\d+)"); // 正则匹配 "shard" 后面的数字，例如 shard1、shard2 ...
                    auto shardBegin = std::sregex_iterator(line.begin(), line.end(), shardRegex);
                    auto shardEnd = std::sregex_iterator();
                    
                    for (std::sregex_iterator i = shardBegin; i != shardEnd; ++i) {
                        std::smatch match = *i;
                        // match[1] 是捕获的数字部分
                        int shardNum = std::stoi(match[1].str());
                        data.shardNums.push_back(shardNum);
                    }

                    // 正则匹配冒号后面的数字, 例如 :1600
                    std::regex colonRegex(":(\\d+)");
                    std::smatch colonMatch;
                    if (std::regex_search(line, colonMatch, colonRegex)) {
                        data.txnum = std::stoi(colonMatch[1].str());
                    } else {
                        data.txnum = -1; // 如果未找到则设为 -1
                    }

                    //更新 cooshard_cstNums
                    int shard1 = data.shardNums.at(0);
                    int shard2 = data.shardNums.at(1);
                    int lca = findLCA(shard1, shard2); // shard1和shard2的最近公共祖先
                    data.lca = lca;

                    int cstNum = data.txnum;
                    cooshard_cstNums[lca] += cstNum;
                    Cross_workloads.push_back(data);
                }
            }

            // // 打印 cooshard_cstNums 信息
            // for(auto cooshard_cstNum : cooshard_cstNums){
            //     int cooshardid = cooshard_cstNum.first;
            //     int txNum = cooshard_cstNum.second;
            //     cout << "分片" << cooshardid << "需要排序的跨片交易总数为" << txNum << endl;
            // }
        }

        // void extract_topologyLines(vector<string> topologyLines){
        //     for(auto line : topologyLines){
        //         auto ids = parseTopologyLine(line);

        //         for(auto id : ids){
        //             if(find(leafshardids.begin(), leafshardids.end(), id) == leafshardids.end() && // leafshardids 所有叶子结点id, cooshardids 表示所有的排序结点
        //                     find(cooshardids.begin(), cooshardids.end(), id) == cooshardids.end() ){
        //                 cooshardids.push_back(id);
        //             }
        //         }

        //         // 构建拓扑结构
        //         int parentId = ids.at(0);
        //         int ids_size = ids.size();
        //         for(int i = 1; i < ids_size; i++){ // 添加孩子分片
        //             int childId = ids.at(i);
        //             ParentChildMaps[parentId].push_back(childId);
        //         }

        //         // 为父亲分片添加孩子分片
        //         for(int i = 1; i < ids_size; i++){ // 添加孩子分片
        //             int childId = ids.at(i);
        //             topology_relationship[parentId].childrenids.push_back(childId);
        //             topology_relationship[childId].parentid = parentId;
        //         }
        //     }

        //     // 为顶层分片设置 parent = -1
        //     for (auto& entry : topology_relationship) {
        //         if (entry.second.parentid == -1 && entry.second.childrenids.empty() == false) {
        //             entry.second.parentid = -1;
        //         }
        //     }
            
        //     // // 抽取到的上层分片有
        //     // cout << "抽取到的上层分片有: ";
        //     // for(auto cooshardid : cooshardids){
        //     //     cout << cooshardid << ", ";
        //     // }
        //     // cout << endl;
        // }

        // vector<int> parseTopologyLine(const string& line){ // 解析
        //     vector<int> numbers;
        //     // 匹配整数的正则表达式
        //     regex re("-?\\d+");
        //     auto begin = sregex_iterator(line.begin(), line.end(), re);
        //     auto end = sregex_iterator();
            
        //     for (auto i = begin; i != end; ++i) {
        //         smatch match = *i;
        //         int num = stoi(match.str());
        //         numbers.push_back(num);
        //     }
        //     return numbers;
        // }

        int findLCA(int shard1, int shard2){ // 7 & 9
            set<int> ancestors;
            while (shard1 != -1) {
                ancestors.insert(shard1);
                shard1 = topology_relationship[shard1].parentid;
            }
            while (shard2 != -1) {
                if (ancestors.count(shard2)) return shard2; // 找到最近公共祖先
                shard2 = topology_relationship[shard2].parentid;
            }
            return -1; // 不可能发生
        }

};


int main(){

    vector<string> loadLines = {
        "shard1_inner:2000", "shard2_inner:2000", "shard3_inner:2000", "shard4_inner:2000",
        "shard5_inner:2000", "shard6_inner:2000", "shard7_inner:2000", "shard8_inner:2000",
        "shard9_inner:2000", "shard10_inner:2000", "shard11_inner:2000", "shard12_inner:2000",
        "shard13_inner:2000", "shard14_inner:2000", "shard15_inner:2000", "shard16_inner:2000",
        "shard17_inner:2000", "shard18_inner:2000", "shard19_inner:2000", "shard20_inner:2000",
        "shard21_inner:2000", "shard22_inner:2000", "shard23_inner:2000", "shard24_inner:2000",
        "shard25_inner:2000", "shard26_inner:2000", "shard27_inner:2000", "shard28_inner:2000",
        "shard29_inner:2000", "shard30_inner:2000", "shard31_inner:2000", "shard32_inner:2000",
        "shard33_inner:2000", "shard34_inner:2000", "shard35_inner:2000", "shard36_inner:2000",

        "shard1_shard2:2000", "shard2_shard3:2000", "shard3_shard4:2000", "shard5_shard6:2000",
        "shard5_shard7:2000", "shard6_shard7:2000", "shard8_shard9:2000", "shard1_shard3:2000",
        "shard4_shard8:2000", "shard9_shard10:2000", "shard10_shard11:2000", "shard12_shard13:2000",
        "shard4_shard7:2000", "shard22_shard10:2000", "shard17_shard11:2000", "shard12_shard33:2000",
        "shard14_shard15:2000", "shard16_shard17:2000", "shard17_shard18:2000", "shard19_shard20:2000",
        "shard21_shard22:2000", "shard12_shard14:2000", "shard16_shard19:2000", "shard20_shard5:2000",
        "shard10_shard23:2000", "shard23_shard24:2000", "shard25_shard26:2000", "shard23_shard24:2000", 
        "shard27_shard5:2000", "shard18_shard28:2000", "shard11_shard32:2000", "shard1_shard34:2000",
        "shard27_shard28:2000", "shard29_shard30:2000", "shard31_shard32:2000", "shard33_shard34:2000",
    };

    int leafShardCount = 3; // 启动叶子分片
    int totalShardCount = 4;
    int CoordinatorShardCount = 1; // 所有分片个数


    // // 初始化叶子分片数组
    // thread_safety_map <int, Shard*> shards;
    // for(int shardid = 1; shardid <= leafShardCount; shardid++){
    //     map<string, string> permission_list; // 权限列表
    //     int state_number = 1000; // 分片中的状态数量

    //     srand(unsigned(time(0)));
    //     for(int i = 0; i < state_number; i++){
    //         int stateid = i + (shardid - 1) * state_number;
    //         string key = "state_" + to_string(stateid);
    //         string value = to_string(rand() % 100);
    //         permission_list.insert(make_pair(key, value)); // 权限列表
    //     }

    //     // int order_capability = 5000;
    //     // int execution_capability = 8000;
    //     // int processBatch = 5000;
    //     // int tx_sendRate = 2000;
    //     Shard* shard = new Shard(shardid, orderingCapacity, executionCapacity, batchFetchSize, transactionSendRate, permission_list);
    //     shards.insert(shardid, shard); // 初始化分片
    // }


    ShardsManager* manager = new ShardsManager(leafShardCount, CoordinatorShardCount, accessControlListDir);
    manager->parseTopology();
    manager->printShardTopology();
    manager->parseWorkload();
    manager->printWorkload();
    
    manager->startAllShards();

    // std::this_thread::sleep_for(std::chrono::milliseconds(10000)); // sleep10秒，等待全部分片启动完毕

    // globalPerformanceStats* status = new globalPerformanceStats(leafShardCount, totalShardCount); // 启动监控TPS和延迟的线程，每秒打印
    // status->startMetrics();






    // // 启动叶子分片
    // for(int shardid = 1; shardid <= leaf_shard_number; shardid++){
    //     auto shard = shards.find(shardid);
    //     cout << "启动分片" << shard->shardId << "..." << endl;
    //     shard->start();
    // }

    // // 启动上层分片
    // vector<int> leafshardids = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36};
    // topology_analyzer analyzer(leafshardids);

    // analyzer.extract_topologyLines(topologyLines);
    // analyzer.extract_workloadLines(loadLines);

    // // 开始准备生成排序者分片
    // int order_capability = 5000;
    // int processBatch = 5000;
    // // int tx_sendRate = 4000;

    // // 开始生成排序者对象
    // vector<shared_ptr<CoordinatorShard>> coord_shards;
    // auto cooshard_cstNums = analyzer.cooshard_cstNums; // 排序任务
    // auto cross_workloads = analyzer.Cross_workloads; // 跨片负载

    // for(auto cooshard_cstNum: cooshard_cstNums){
    //     int coord_shardid = cooshard_cstNum.first;
    //     int cstNum = cooshard_cstNum.second;
    //     vector<pair<pair<int, int>, double>> destination_pairs;

    //     cout << "coord_shardid = " << coord_shardid << ", cstNum = " << cstNum << endl;

    //     for(auto cross_workload: cross_workloads){
    //         if(cross_workload.lca == coord_shardid){
    //             int shard1 = cross_workload.shardNums.at(0);
    //             int shard2 = cross_workload.shardNums.at(1);
    //             double percentage = double(cross_workload.txnum) / cooshard_cstNums.at(coord_shardid);
    //             destination_pairs.push_back(make_pair(make_pair(shard1, shard2), percentage));
    //         }
    //     }

    //     int tx_sendRate = cstNum;
    //     auto cooshard = make_shared<CoordinatorShard>(coord_shardid, tx_sendRate, processBatch, order_capability, destination_pairs);
    //     coord_shards.push_back(cooshard);
    // }

    // for(auto coord_shard : coord_shards){
    //     coord_shard->start(shards); // 启动上层分片
    // }

    // 主线程常驻
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    return 0;
}