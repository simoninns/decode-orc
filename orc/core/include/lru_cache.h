#pragma once

#include <unordered_map>
#include <list>
#include <optional>
#include <functional>
#include <mutex>

namespace orc {

/**
 * @brief Simple LRU (Least Recently Used) cache
 * 
 * Thread-safe: Yes. Uses internal mutex for all operations.
 * 
 * @tparam Key Key type (must be hashable)
 * @tparam Value Value type
 * @tparam Hash Hash function type (defaults to std::hash<Key>)
 */
template<typename Key, typename Value, typename Hash = std::hash<Key>>
class LRUCache {
public:
    /**
     * @brief Construct LRU cache with maximum size
     * @param max_size Maximum number of entries to keep in cache
     */
    explicit LRUCache(size_t max_size) : max_size_(max_size) {}
    
    // Disable copy and move - cache contains mutex and is not safely copyable
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;
    LRUCache(LRUCache&&) = delete;
    LRUCache& operator=(LRUCache&&) = delete;
    
    /**
     * @brief Get value from cache
     * @param key Key to look up
     * @return Value if found, std::nullopt otherwise
     */
    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }
        
        // Move to front of LRU list (most recently used)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        
        return it->second->second;
    }
    
    /**
     * @brief Get pointer to value in cache (for large values like vectors)
     * @param key Key to look up
     * @return Pointer to value if found, nullptr otherwise
     * @note The pointer is valid until the entry is evicted from cache
     */
    const Value* get_ptr(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return nullptr;
        }
        
        // Move to front of LRU list (most recently used)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        
        return &(it->second->second);
    }
    
    /**
     * @brief Insert or update value in cache
     * @param key Key to insert
     * @param value Value to store
     */
    void put(const Key& key, Value value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        
        if (it != map_.end()) {
            // Key exists - update value and move to front
            it->second->second = std::move(value);
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
            return;
        }
        
        // Evict oldest if at capacity
        if (lru_list_.size() >= max_size_) {
            auto oldest = lru_list_.back();
            map_.erase(oldest.first);
            lru_list_.pop_back();
        }
        
        // Insert new entry at front (most recently used)
        lru_list_.emplace_front(key, std::move(value));
        map_[key] = lru_list_.begin();
    }
    
    /**
     * @brief Check if key exists in cache (without updating LRU order)
     * @param key Key to check
     * @return True if key exists
     */
    bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.find(key) != map_.end();
    }
    
    /**
     * @brief Clear all entries from cache
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
        lru_list_.clear();
    }
    
    /**
     * @brief Get current number of entries in cache
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lru_list_.size();
    }
    
    /**
     * @brief Get maximum cache size
     */
    size_t max_size() const {
        return max_size_;
    }
    
private:
    size_t max_size_;
    
    // List of (key, value) pairs in LRU order (front = most recent)
    mutable std::list<std::pair<Key, Value>> lru_list_;
    
    // Map from key to iterator in lru_list_ (uses custom hash function)
    mutable std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator, Hash> map_;
    
    // Mutex for thread-safe access
    mutable std::mutex mutex_;
};

} // namespace orc
