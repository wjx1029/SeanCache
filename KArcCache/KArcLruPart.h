# pragma once

# include "KArcCacheNode.h"
# include <unordered_map>
# include <mutex>

namespace SeanCache
{

template<typename Key, typename value>
class ArclruPart
{
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<key, NodePtr>;

    explicit (size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
    {
        initializeLists();
    }

    bool put(Key key, Value value)
    {
        if (capacity_ == 0) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    bool get(Key key, Value& value, bool& shouldTransform = false)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            shouldTransform = updateNodeAccess(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    bool checkGhost(Key key)
    {
        auto it = ghostCache_.find(key);
        if (it != ghostCache_.end())
        {
            removeFromList(it->second);
            ghostCache_.erase(it);
            return true;
        }
        return false;
    }

    void increaseCapacity() { ++capacity_; }

    bool decreaseCapacity()
    {
        if (capacity_ <= 0) return false;
        if (mainCache_.size() == capacity_)
        {
            evictLeastRecent();
        }
        --capacity_;
        return true;
    }


private:
    void initializeLists()
    {
        mainHead_ = std::make_shared<NodeType>();
        mainTail_ = std::make_shared<NodeType>();
        mainHead_->next = mainTail_;
        mainTail_->prev = mainHead_;

        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next = ghostTail_;
        ghostTail_->prev = ghostHead_;
    }

    bool updateExistingNode(NodePtr node, Value value)
    {
        if (node == nullptr) return false;

        node->setValue(value);
        moveToFront(node);
        return true;
    }

    bool addNewNode(const Key& key, const Value value)
    {
        if (mainCache_.size() >= capacity_)
        {
            evictLeastRecent(); // 驱逐最近最少访问,即链表末尾节点
        }

        NodePtr new_node = std::make_shared<NodePtr>(key, value);
        mainCache_[key] = new_node;
        addToFront(new_node);

        return true;
    }

    bool updateNodeAccess(NodePtr node)
    {
        moveToFront(node);
        node->incrementAccessCount();
        return node->getAccessCount() >= transformThreshold_;
    }

    void moveToFront(NodePtr node)
    {
        // 先从当前位置移除
        removeFromList(node);

        // 添加到头部
        addToFront(node);
    }

    void addToFront(NodePtr node)
    {
        node->next_ = mainHead_->next_;
        mainHead_->next_->prev_ = node;
        mainHead_->next_ = node;
        node->prev_ = mainHead_;
    }

    void evictLeastRecent()
    {
        NodePtr leastRecentNode = mainTail_->prev_.lock();

        if (!leastRecentNode || leastRecentNode == mainHead_)
            return;

        // 从主链表移除
        removeFromList(leastRecentNode);

        // 添加到幽灵缓存
        if (ghostCache_.size() >= ghostCapacity_)
        {
            evictOldestGhost();
        }
        addToGhost(leastRecentNode);

        // 从主缓存映射中删除
        mainCache_.erase(leastRecentNode->getKey());
    }

    void evictOldGhost()
    {
        NodePtr oldestGhostNode = ghostTail_->prev_.lock();

        if (!oldestGhostNode || oldestGhostNode == ghostHead_)
            return;

        // 从幽灵链表删除
        removeFromList(oldestGhostNode);
        ghostCache_.erase(oldestGhostNode->getKey());
    }

    void addToGhost(NodePtr node)
    {
        // 重置节点访问次数
        node->accessCount_ = 1;

        // 添加到幽灵缓存的头部
        node->next_ = ghostHead_->next_;
        ghostHead_->next_->prev_ = node;
        ghostHead_->next_ = node;
        node->prev_ = ghostHead_;

        // 添加到幽灵缓存映射
        ghostCache_[node->getKey()] = node;
    }

    void removeFromList(NodePtr node)
    {
        if (!node->prev_.expired() && node->next_)
        {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            // 清空指针，防止悬垂引用
            node->next_ = nullptr;
            node->prev_.reset();
        }
    }

private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_; // 转换门槛
    std::mutex mutex_;

    NodeMap mainCache_;     // key -> ArcNode
    NodeMap ghostCache_;

    // 主链表
    NodePtr mainHead_;
    NodePtr mainTail_;
    // 淘汰链表
    NodePtr ghostHead_;
    NodePtr ghostTail_;

};

}