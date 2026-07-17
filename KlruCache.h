# pragma once

# include <memory>
# include <unordered_map>
# include <mutex>
# include <vector>

# include "KICachePolicy.h"

namespace SeanCache
{

// 前向声明
template<typename Key, typename Value> 
class KLrucache;

// LruNode 类
template<typename Key, typename Value> 
class LruNode
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;    // 访问次数
    std::weak_ptr<LruNode<Key, Value>> prev_;   // 改为weak_ptr打破循环引用
    std::shared_ptr<LruNode<Key, Value>> next_;

public:
    LruNode(Key key, Value value):
        key_(key),
        value_(value),
        accessCount_(1)
        {}

    // 提供必要的访问器
    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    void setValue(const Value& value) { value_ = value; }
    size_t getAccessCount() const { return accessCount_; }
    void incrementAccessCount() { ++accessCount_; }

    friend class KLruCache<Key, Value>;

};


// KLruCache 类
template<typename Key, typename Value> 
class KLruCache : public KICachePolicy<Key, Value>
{
public:
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    // 构造函数
    KLruCache(int capacity) : capacity_(capacity) 
    {
        initializelist();
    }

    // 析构函数
    ~KLruCache() override = default;

    // 添加缓存
    void put(Key key, Value value) override
    {
        if (capacity_ <= 0) return;

        // lock对象在构造时自动锁定mutex_，在析构时自动解锁。
        // 作用域：从声明处到当前代码块（{}）结束。
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            // 如果在当前容器中,则更新value,并调用moveToMostRecent方法，将节点移动到链表尾部,代表该数据刚被访问
            updateExistingNode(it->second, value);
            return;
        }

        addNewNode(key, node);
    }

    // 访问缓存
    bool get(Key key, Value& value) override
    {
        // lock对象在构造时自动锁定mutex_，在析构时自动解锁。
        // 作用域：从声明处到当前代码块（{}）结束。
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) 
        {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(Key key) override
    {
        Value value{};
        get(key, value);
        return value;
    }

    // 删除缓存
    void remove(Key key) 
    {
        // lock对象在构造时自动锁定mutex_，在析构时自动解锁。
        // 作用域：从声明处到当前代码块（{}）结束。
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) 
        {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }

private:
    void initializeList()
    {
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummytail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_ -> next = dummytail_;
        dummytail_ -> prev = dummyHead_;
    }

    void updateExistingNode(NodePtr p_node, const Value& value)
    {
        p_node->setValue(value);
        // p_node->incrementAccesscount();
        moveToMostRecent(p_node);
    }

    // 将该节点移动到最新的位置
    void moveToMostRecent(NodePtr p_node)
    {
        removeNode(p_node);
        insertNode(p_node);
    }

    // 删除节点
    void removeNode(NodePtr p_node) 
    {
        if (!p_node->prev_.expired() && p_node->next_)
        {
            auto prev = p_node->prev_.lock(); // 使用lock()获取shared_ptr
            prev->next_ = p_node->next_;
            p_node->next_->prev_ = prev;
            p_node->next_ = nullptr;         // 清空next_指针,彻底断开连接
        }        
    }

    // 从尾部插入节点
    void insertNode(NodePtr p_node)
    {
        p_node->next_ = dummytail_;
        p_node->prev_ = dummytail_-> prev_;
        dummytail_->prev.lock()->next_() = p_node;  // 使用lock()获取shared_ptr
        dummytail_->prev_ = p_node;

    }

    // 在缓存链表尾部插入新节点,并更新mapNode映射
    void addNewnode(const Key& key, const Value& value)
    {
        if (nodeMap_.size() >= capacity_)
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode
    }

    // 驱逐最近最少访问的节点,即链表头部节点,并删除nodeMap映射
    void evictLeastRecent()
    {
        NodePtr leastRecent = dummyHead_->next_;
        removeNode(leastRecent);
        nodeMap_.erase(leastRecent->getKey());
    }

private:
    int capacity_;      // 缓存空间大小
    NodeMap nodeMap_;   // key -> LruNode
    std::mutex mutex_;  
    NodePtr dummyHead_; // 虚拟头结点
    NodePtr dummytail_; 
};





// LRU优化：Lru-k版本。 通过继承的方式进行再优化
/*
使用两个链表:
1. 主链表(主缓存): 用于缓存数据,继承自 KLruCache<Key, Value>
2. 历史记录链表: 链表节点记录每个key-value对被访问次数,只有达到k次,才将数据放入主缓存
*/
template<typename Key, typename Value>
class KLruKCache : public KLruCache<Key, Value>
{
public:

    // 构造函数
    KLruKCache(int capacity, int historyCapacity, int k)
        : KLruCache<key, Value>(capacity)   // 调用基类构造
        , historyList_(std::make_unique<KLruCache<Key, size_t>(historyCapacity))
        , k_(k)
        {}

    Value get(Key key)
    {
        // 首先尝试在主缓存获取数据
        Value value{};
        bool inMaincache = LruCache<Key, Value>::get(key, value);

        // 获取并更新访问历史记录
        size_t historyAccessCount = historyList_->get(key);
        historyAccessCount++;
        historyList_->put(key, historyAccessCount);

        // 如果数据在主缓存中,直接返回
        if (inMaincache)
        {
            return value;
        }

        // 如果数据不在主缓存，但访问次数达到了k次
        if (historyAccessCount >= k_)
        {
            // 检查是否有历史值记录
            auto it = historyValueMap_.find(key);
            if (it != historyValueMap_.end())
            {
                // 把数据放入主缓存
                Value storedValue = it->second;
                KLruCache<Key, Value>::put(key, storedvalue);

                // 删除历史值记录
                historyValueMap_.erase(it);
                historyList_->remove(key);

                return storedValue;
            }
        }

        // 数据不在主缓存, 且不满足添加条件, 返回默认值
        return value;

    }

    void put(Key key, Value value)
    {
        // 检查是否已经在主缓存
        Value existedValue{};
        bool inMainCache = KLruCache<Key, Value>::get(key, existedValue);

        // 已经在主缓存,直接更新
        if (inMainCache)
        {
            KLruCache<Key, Value>::put(key, value);
            return;
        }

        // 获取并更新访问历史记录
        size_t historyAccessCount = historyList_->get(key);
        historyAccessCount++;
        historyList_->put(key, historyAccessCount);

        // 保存值到历史记录映射, 供后续get操作使用
        historyValueMap_[key] = value;

        // 检查是否达到k次访问
        if (historyAccessCount >= k_)
        {
            historyValueMap_.erase(key);
            historyList_->remove(key);
            KLruCache<Key, Value>::put(key, value);
        }

    }

private:
    int                                     k_;                  // 进入缓存队列的评判标准
    std::unique_ptr<KLruCache<Key, size_t>> historyList_;        // 访问数据历史记录(value为访问次数)
    std::unordered_map<Key, Value>          historyValueMap_     // 存储未达到k次访问的数据值
};


/*
普通的lruCache和lfuCache在高并发情况下耗时增加的原因分析：
线程安全的lfuCache中有锁的存在。
每次读写操作之前都有加锁操作，完成读写操作之后还有解锁操作。
在低QPS下，锁的竞争的耗时基本可以忽略；
但在高并发的情况下，大量的时间消耗在等待锁的操作上，导致耗时增长
*/

// 针对大量同步等待操作导致耗时增加的情况，解决方案就是尽量减小临界区。
// 引入hash机制，对全量数据做分片处理，在原有LfuCache的基础上形成HashLfuCache，以降低查询耗时。

// lru优化: 对lru进行分片, 提高高并发使用性能
template<typename Key, typename Value>
class KHashLrucache
{
public:
    KHashLrucache(size_t capacity, int sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())  //作用：返回系统支持的硬件并发线程数（即 CPU 核心数）。返回值：unsigned int（无符号整数）。
    {
        // 获取每个分片的大小
        size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
        //                      ^^^^  ^^^^^^^^     ^^^^^^^^^^^^^^^^^^^^^^^^^
        //                     向上取整  整数        转为 double，避免整数除法
        for (int i = 0; i < sliceNum_; ++i)
        {
            lruSliceCaches_.emplace_back(new LruCache<Key, Value>(sliceSize));
        }
    }

    void put(Key key, Value value)
    {
        // 获取key的hash值，并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        lruSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value)
    {
        // 获取key的hash值，并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        lruSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key)
    {
        Value value{};
        memset(&value, 0, sizeof(value));
        get(key, value);
        return value;
    }

private:
    // 将key转换为对应hash值
    size_t Hash(Key key)
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }



private:
    size_t                                              capacity_;   // 总容量
    int                                                 sliceNum_;  // 切片数量
    std::vector<std::unique_ptr<KLruCache<Key, value>>> lruSliceCaches_;    // 切片lru缓存
};

} // namespace SeanCache