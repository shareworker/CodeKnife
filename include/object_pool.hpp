#ifndef UTIL_OBJECT_POOL_HPP
#define UTIL_OBJECT_POOL_HPP

#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <type_traits>
#include <cassert>
#include <atomic>

namespace SAK {
namespace pool {

/**
 * @brief Growth policy for the object pool.
 * Determines how the pool grows when it needs more objects.
 */
enum class GrowthPolicy {
    /// Double the size of the pool when more objects are needed
    Multiplicative,
    /// Add a fixed number of objects when more objects are needed
    Additive,
    /// Don't grow the pool automatically, throw an exception when empty
    Fixed
};

/**
 * @brief Thread-safe object pool implementation.
 * 
 * Provides efficient object reuse with customizable growth policies.
 * Supports both objects with and without reset methods.
 * 
 * @tparam T The type of objects to pool
 * @tparam ResetFunction Type of function used to reset objects (defaults to no-op)
 */
template <typename T, typename ResetFunction = std::function<void(T&)>>
class ObjectPool {
public:
    /**
     * @brief Construct a new Object Pool with default settings
     * 
     * @param initial_size Initial number of objects in the pool
     * @param growth_policy How the pool should grow when empty
     * @param growth_size Size parameter for growth (amount to add or multiply by)
     * @param reset_func Function to reset objects when returned to the pool
     */
    explicit ObjectPool(
        size_t initial_size = 32,
        GrowthPolicy growth_policy = GrowthPolicy::Multiplicative,
        size_t growth_size = 2,
        ResetFunction reset_func = [](T&) {}
    ) : growth_policy_(growth_policy),
        growth_size_(growth_size),
        reset_func_(reset_func),
        active_count_(0)
    {
        // Pre-allocate objects
        objects_.reserve(initial_size);
        for (size_t i = 0; i < initial_size; ++i) {
            objects_.push_back(std::make_unique<T>());
        }
    }

    /**
     * @brief Construct a new Object Pool with in-place construction
     * 
     * @tparam Args Types of arguments to pass to the constructor of T
     * @param initial_size Initial number of objects in the pool
     * @param growth_policy How the pool should grow when empty
     * @param growth_size Size parameter for growth (amount to add or multiply by)
     * @param reset_func Function to reset objects when returned to the pool
     * @param args Arguments to pass to the constructor of T
     */
    template <typename... Args>
    explicit ObjectPool(
        size_t initial_size,
        GrowthPolicy growth_policy,
        size_t growth_size,
        ResetFunction reset_func,
        Args&&... args
    ) : growth_policy_(growth_policy),
        growth_size_(growth_size),
        reset_func_(reset_func),
        active_count_(0)
    {
        // Pre-allocate objects with constructor arguments
        objects_.reserve(initial_size);
        for (size_t i = 0; i < initial_size; ++i) {
            objects_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        }
    }

    /**
     * @brief Get an object from the pool
     * 
     * @return T* Pointer to the object (nullptr if allocation failed)
     */
    T* acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (objects_.empty()) {
            if (!grow()) {
                return nullptr; // Growth failed
            }
        }
        
        // Get object from the pool
        std::unique_ptr<T> obj = std::move(objects_.back());
        objects_.pop_back();
        
        // Track for statistics
        ++active_count_;
        
        // Release ownership without deleting
        return obj.release();
    }

    /**
     * @brief Return an object to the pool
     * 
     * @param obj Pointer to the object to return
     */
    void release(T* obj) {
        if (!obj) return;
        
        // Reset the object state if needed
        reset_func_(*obj);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Return to the pool
        objects_.push_back(std::unique_ptr<T>(obj));
        
        // Update statistics
        --active_count_;
    }

    /**
     * @brief Get the number of objects currently in use
     * 
     * @return size_t Number of active objects
     */
    size_t active_count() const {
        return active_count_.load();
    }

    /**
     * @brief Get the number of objects available in the pool
     * 
     * @return size_t Number of available objects
     */
    size_t available_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return objects_.size();
    }

    /**
     * @brief Get the total number of objects managed by the pool
     * 
     * @return size_t Total number of objects
     */
    size_t total_count() const {
        return active_count() + available_count();
    }

    /**
     * @brief Set the growth policy
     * 
     * @param policy New growth policy
     * @param size New growth size parameter
     */
    void set_growth_policy(GrowthPolicy policy, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        growth_policy_ = policy;
        growth_size_ = size;
    }

    /**
     * @brief Set the reset function
     * 
     * @param reset_func New reset function
     */
    void set_reset_function(ResetFunction reset_func) {
        std::lock_guard<std::mutex> lock(mutex_);
        reset_func_ = reset_func;
    }

    /**
     * @brief Reserve capacity for a specific number of objects
     * 
     * @param capacity Desired capacity
     */
    void reserve(size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t current_total = objects_.size() + active_count_.load();
        if (capacity > current_total) {
            size_t to_add = capacity - current_total;
            objects_.reserve(objects_.size() + to_add);
            
            for (size_t i = 0; i < to_add; ++i) {
                objects_.push_back(std::make_unique<T>());
            }
        }
    }

    /**
     * @brief Trim the pool to a specific size
     * 
     * @param target_size Desired size after trimming
     * @return size_t Number of objects removed
     */
    size_t trim(size_t target_size = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (objects_.size() <= target_size) {
            return 0;
        }
        
        size_t to_remove = objects_.size() - target_size;
        objects_.resize(target_size);
        
        return to_remove;
    }

private:
    /**
     * @brief Grow the pool according to the growth policy
     * 
     * @return bool True if growth succeeded, false otherwise
     */
    bool grow() {
        // Must be called with mutex already locked
        
        size_t current_size = objects_.size() + active_count_.load();
        size_t new_objects = 0;
        
        switch (growth_policy_) {
            case GrowthPolicy::Multiplicative:
                new_objects = current_size * (growth_size_ - 1);
                break;
                
            case GrowthPolicy::Additive:
                new_objects = growth_size_;
                break;
                
            case GrowthPolicy::Fixed:
                // Don't grow
                return false;
        }
        
        // Add new objects
        objects_.reserve(objects_.size() + new_objects);
        for (size_t i = 0; i < new_objects; ++i) {
            objects_.push_back(std::make_unique<T>());
        }
        
        return true;
    }

    // Pool storage
    std::vector<std::unique_ptr<T>> objects_;
    
    // Synchronization
    mutable std::mutex mutex_;
    
    // Configuration
    GrowthPolicy growth_policy_;
    size_t growth_size_;
    ResetFunction reset_func_;
    
    // Statistics
    std::atomic<size_t> active_count_;
};

/**
 * @brief RAII wrapper for ObjectPool
 * 
 * Automatically returns the object to the pool when destroyed.
 * 
 * @tparam T The type of object
 * @tparam ResetFunction Type of function used to reset objects
 */
template <typename T, typename ResetFunction = std::function<void(T&)>>
class PooledObject {
public:
    /**
     * @brief Construct a new Pooled Object
     * 
     * @param pool Reference to the object pool
     * @param obj Pointer to the object
     */
    PooledObject(ObjectPool<T, ResetFunction>& pool, T* obj)
        : pool_(pool), obj_(obj) {}
    
    /**
     * @brief Destructor - returns object to the pool
     */
    ~PooledObject() {
        if (obj_) {
            pool_.release(obj_);
            obj_ = nullptr;
        }
    }
    
    /**
     * @brief Move constructor
     */
    PooledObject(PooledObject&& other) noexcept
        : pool_(other.pool_), obj_(other.obj_) {
        other.obj_ = nullptr;
    }
    
    /**
     * @brief Move assignment operator
     */
    PooledObject& operator=(PooledObject&& other) noexcept {
        if (this != &other) {
            if (obj_) {
                pool_.release(obj_);
            }
            
            pool_ = other.pool_;
            obj_ = other.obj_;
            other.obj_ = nullptr;
        }
        return *this;
    }
    
    // Delete copy constructor and assignment
    PooledObject(const PooledObject&) = delete;
    PooledObject& operator=(const PooledObject&) = delete;
    
    /**
     * @brief Get the underlying object
     * 
     * @return T* Pointer to the object
     */
    T* get() const { return obj_; }
    
    /**
     * @brief Dereference operator
     * 
     * @return T& Reference to the object
     */
    T& operator*() const { 
        assert(obj_ != nullptr);
        return *obj_; 
    }
    
    /**
     * @brief Arrow operator
     * 
     * @return T* Pointer to the object
     */
    T* operator->() const { 
        assert(obj_ != nullptr);
        return obj_; 
    }
    
    /**
     * @brief Check if the object is valid
     * 
     * @return true If the object is valid
     * @return false If the object is null
     */
    explicit operator bool() const { return obj_ != nullptr; }

private:
    ObjectPool<T, ResetFunction>& pool_;
    T* obj_;
};

/**
 * @brief Create a pooled object
 * 
 * @tparam T Object type
 * @tparam ResetFunction Reset function type
 * @param pool Object pool
 * @return PooledObject<T, ResetFunction> RAII wrapper for the object
 */
template <typename T, typename ResetFunction>
PooledObject<T, ResetFunction> make_pooled(ObjectPool<T, ResetFunction>& pool) {
    return PooledObject<T, ResetFunction>(pool, pool.acquire());
}

} // namespace pool
} // namespace SAK

#endif // UTIL_OBJECT_POOL_HPP
