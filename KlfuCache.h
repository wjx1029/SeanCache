# pragma once

# include <memory>
# include <mutex>
# include <thread>
# include <unordered_map>
# include <vector>

# include "KICachePolicy.h"

namespace Seancache
{

template<typename Key, typename Value> class KLfuCache;

template<typename Key, typename Value>
class FreqList
{
private:
    struct Node
    {
        int freq;   // 访问频次
        Key key;
        Value value;
        std::weak_ptr<Node> prev;   // 上一节点改为weak_ptr打破循环引用
        std::shared_ptr<Node> next;

        Node()
        : freq(1), next(nullptr) {}

        Node(Key key, Value value)
        : freq(1), key(key), value(value), next(nullptr) {}
    };

    using NodePtr = std::shared_ptr<Node>;
    int freq_;      // 访问频率
    NodePtr head_;  // 虚拟头结点
    NodePtr tail_;  // 虚拟未结点

public:
    explicit FreqList(int n)
    : freq_(n)
    {
        head_ = std::make_shared<Node>();
        tail_ = std::make_shared<Node>();
        head_->next = tail_;
        tail_->prev = head_;
    }

    bool isEmpty() const
    {
        return head_->next == tail_
    }

    void addNode(NodePtr node)
    {
        if (!node || !head_ || !tail_)
            return;
        
        node->prev = tail_->prev;
        node->next = tail_;
        tail_->prev.lock()->next = node;    //  weak_ptr 必须通过 lock() 获取 shared_ptr 才能访问
        tail_->prev = node;
    }

    void removeNode(NodePtr node)
    {
        if (!node || !head_ || !tail_)
            return;
        if (node->prev.expired() || !node->next)
            return;
        
        auto pre = node->prev.lock();
        pre->next = node->next;
        node->next->prev = pre;
        node->next = nullptr;   // 确保显式置空next指针，彻底断开节点与链表的连接
    }

    NodePtr getFirstNode() const {return head_->next;}

    friend class KLfuCache<Key, Value>;
};


template <typename Key, typename Value>
class KLfuCache : public KICachePolicy<Key, Value>
{
public:
    using Node = typename FreqList<Key, Value>::Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    KLfuCache(int capacity, int maxAverageNum = 1000000)
    : capacity_(capacity)
    , minFreq_(INT8_MAX)
    , maxAverageNum_(maxAverageNum)
    , curAverageNum_(0)
    , curTotalNUM_(0)
    {}

    ~KLfuCache() override = default;

    void put(Key key, Value value) override
    {

    }

    bool get(Key key, Value& value) override
    {

    }

    Value get(Key key) override
    {

    }

    // 清空缓存, 回收资源
    void purge()
    {

    }

private:
    void putInternal(Key key, Value value);         // 添加缓存
    void getInternal(NodePTr node, Value& value);   // 获取缓存

    void kickOut();                                 // 移除缓存中的过期数据

    void removeFromFreqList(NodePtr node);          // 从频率列表中移除节点
    void addtoFreqlist(NodePtr node);               // 添加到频率列表

    void addFreqNum();                              // 增加平均访问等频率
    void decreaseFreqNum(int num);                  // 减少平均访问等频率
    void handleOverMaxAverageNum();                 // 处理当前平均访问频率超过上限的情况
    void updateMinFreq();

private:
    int                                           capacity_;        // 缓存容量
    int                                           minFreq_;         // 最小访问频次(用于找到最小访问频次结点)
    int                                           maxAveragenum_;   // 最大平均访问频次
    int                                           curAverageNum_;   // 当前平均访问频次
    int                                           curtotalNum_;     // 当前访问所有缓存次数总数 
    std::mutex                                    mutex_;           // 互斥锁
    NodeMap                                       nodeMap_;         // key 到 缓存节点的映射
    std::unordered_map<int, FreqList<Key, Value>> freqToFreqList_;  // 访问频次到该频次链表的映射
};

} // namespace