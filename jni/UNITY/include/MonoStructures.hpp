#pragma once

#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>

// ---------------------------------------------------------------------------
// DataIterator  –  safe wrapper for pointers coming out of Array / List
// ---------------------------------------------------------------------------
template<typename T>
struct DataIterator {
    T *value{};

    constexpr DataIterator() = default;
    constexpr DataIterator(const T *value) : value((T *)value) {}

    inline T& operator *()              { return *value; }
    inline T& operator *()       const  { return *value; }
    inline operator T&()                { return *value; }
    inline operator T&()         const  { return *value; }
    inline T& operator ->()             { return *value; }
    inline T& operator ->()      const  { return *value; }

    inline DataIterator &operator=(T t) {
        *this->value = *(T*)&t;
        return *this;
    }
    inline DataIterator &operator=(T t) const {
        *this->value = *(T*)&t;
        return *this;
    }
};

// ---------------------------------------------------------------------------
// namespace Unity
// ---------------------------------------------------------------------------
namespace Unity {

// Forward declaration needed by List
template<typename T> struct Array;

// -------------------------------------------------------------------------
// Array<T>
// -------------------------------------------------------------------------
template<typename T>
struct Array : Il2CppObject {
    Il2CppArrayBounds       *bounds{};
    il2cpp_array_size_t      capacity{};
    T                        m_Items[0];

    // --- Accessors ---

    [[nodiscard]] inline il2cpp_array_size_t GetCapacity() const {
        if (!this) return 0;
        return capacity;
    }

    /** Alias of GetCapacity(). */
    [[nodiscard]] inline il2cpp_array_size_t GetSize() const { return GetCapacity(); }

    [[nodiscard]] inline T *GetData() const {
        if (!this) return nullptr;
        return (T *const) &m_Items[0];
    }

    [[nodiscard]] std::vector<T> ToVector() const {
        std::vector<T> ret;
        if (!this) return ret;
        for (il2cpp_array_size_t i = 0; i < capacity; i++) ret.push_back(m_Items[i]);
        return ret;
    }

    // --- Copy helpers ---

    inline bool CopyFrom(const std::vector<T> &vec) {
        if (!this || vec.empty()) return false;
        return CopyFrom((T *)vec.data(), (il2cpp_array_size_t)vec.size());
    }

    bool CopyFrom(T *arr, il2cpp_array_size_t size) {
        if (!this) return false;
        if (size > capacity) return false;
        memcpy(&m_Items[0], arr, size * sizeof(T));
        return true;
    }

    inline void CopyTo(T *arr) const {
        if (!this || !arr) return;
        memcpy(arr, m_Items, sizeof(T) * capacity);
    }

    // --- Element access ---

    [[nodiscard]] inline DataIterator<T> At(il2cpp_array_size_t index) const {
        if (!this || GetCapacity() < index) return {};
        return &m_Items[index];
    }
    inline DataIterator<T> operator[](il2cpp_array_size_t index) const { return At(index); }

    [[nodiscard]] inline bool Empty() const {
        if (!this) return false;
        return GetCapacity() <= 0;
    }

    // --- Static factory methods (malloc-based, no Il2Cpp allocator) ---

    /**
        @brief Create an empty array of the given capacity.
        @note Caller owns the memory – free with Destroy().
    */
    static Array<T> *Create(size_t cap) {
        auto *unityArr = (Array<T> *) malloc(sizeof(Array<T>) + sizeof(T) * cap);
        if (!unityArr) return nullptr;
        memset(unityArr, 0, sizeof(Array<T>) + sizeof(T) * cap);
        unityArr->capacity = (il2cpp_array_size_t)cap;
        return unityArr;
    }

    static Array<T> *Create(const std::vector<T> &vec) {
        return Create((T *)vec.data(), vec.size());
    }

    static Array<T> *Create(T *arr, size_t size) {
        Array<T> *unityArr = Create(size);
        if (unityArr) unityArr->CopyFrom(arr, (il2cpp_array_size_t)size);
        return unityArr;
    }

    /** @brief Free memory allocated by Create(). Only call on arrays you own. */
    inline void Destroy() { free(this); }

    inline constexpr Array() : Il2CppObject() {}
};

// -------------------------------------------------------------------------
// List<T>  (mirrors System.Collections.Generic.List<T>)
// -------------------------------------------------------------------------
template<typename T>
struct List : Il2CppObject {

    // --- Enumerator ---
    struct Enumerator {
        List<T> *list{};
        int      index{};
        int      version{};
        T        current{};

        constexpr Enumerator() = default;
        explicit Enumerator(List<T> *list) : Enumerator() { this->list = list; }

        inline T *begin()               { return &list->items->m_Items[0]; }
        inline T *end()                 { return &list->items->m_Items[list->size]; }
        [[nodiscard]] inline T *begin() const { return &list->items->m_Items[0]; }
        [[nodiscard]] inline T *end()   const { return &list->items->m_Items[list->size]; }
    };

    Array<T> *items{};
    int       size{};
    int       version{};
    void     *syncRoot{};

    // --- Accessors ---

    [[nodiscard]] inline T   *GetData()     const { return items ? items->GetData() : nullptr; }
    [[nodiscard]] inline int  GetSize()     const { return size; }
    [[nodiscard]] inline int  GetCapacity() const { return items ? (int)items->GetCapacity() : 0; }
    [[nodiscard]] inline int  GetVersion()  const { return version; }

    [[nodiscard]] std::vector<T> ToVector() const {
        std::vector<T> ret{};
        if (!this || !items) return ret;
        for (int i = 0; i < size; i++) ret.push_back(GetData()[i]);
        return ret;
    }

    // --- Mutation ---

    void Add(T val) {
        GrowIfNeeded(1);
        items->m_Items[size] = val;
        ++size; ++version;
    }

    [[nodiscard]] int IndexOf(T val) const {
        for (int i = 0; i < size; i++) if (items->m_Items[i] == val) return i;
        return -1;
    }

    void RemoveAt(int index) {
        if (index != -1) { Shift(index, -1); ++version; }
    }

    bool Remove(T val) {
        int i = IndexOf(val);
        if (i == -1) return false;
        RemoveAt(i);
        return true;
    }

    /**
        @brief Resize internal array to at least newCapacity.
        @return True if resize was performed.
    */
    bool Resize(int newCapacity) {
        if (!items || newCapacity <= (int)items->capacity) return false;
        auto *nItems = Array<T>::Create((size_t)newCapacity);
        if (!nItems) return false;
        nItems->klass   = items->klass;
        nItems->monitor = items->monitor;
        nItems->bounds  = items->bounds;
        if (items->capacity > 0)
            memcpy(&nItems->m_Items[0], &items->m_Items[0], items->capacity * sizeof(T));
        items = nItems;
        return true;
    }

    // --- Element access ---

    [[nodiscard]] DataIterator<T> At(int index) const {
        if (index >= size) return {};
        return &items->m_Items[index];
    }
    DataIterator<T> operator[](int index) const { return At(index); }

    [[nodiscard]] T get_Item(int index) const {
        if (index >= size) return {};
        return items->m_Items[index];
    }

    void set_Item(int index, T item) {
        if (index >= size) return;
        items->m_Items[index] = item;
        ++version;
    }

    void Insert(int index, T item) {
        if (index > size) return;
        if (size == (int)items->capacity) GrowIfNeeded(1);
        if (index < size)
            memmove(items->m_Items + index + 1,
                    items->m_Items + index,
                    (size - index) * sizeof(T));
        items->m_Items[index] = item;
        ++size; ++version;
    }

    // --- Copy helpers ---

    inline bool CopyFrom(const std::vector<T> &vec) {
        return CopyFrom((T *)vec.data(), (int)vec.size());
    }

    bool CopyFrom(T *arr, int arrSize) {
        if (!this) return false;
        Resize(arrSize);
        memcpy(items->m_Items, arr, arrSize * sizeof(T));
        return true;
    }

    void CopyTo(Array<T> *arr, int arrIndex) const {
        memcpy(items->m_Items, arr->m_Items + arrIndex, size * sizeof(T));
    }

    void Clear() {
        if (size > 0) memset(items->m_Items, 0, size * sizeof(T));
        ++version; size = 0;
    }

    [[nodiscard]] bool Contains(T item) const {
        for (int i = 0; i < size; i++) if (items->m_Items[i] == item) return true;
        return false;
    }

    Enumerator GetEnumerator() { return Enumerator(this); }

    // --- Internal helpers ---

    void GrowIfNeeded(int n) {
        if (!items || size + n > (int)items->capacity)
            Resize(size + n + 4); // +4 to avoid constant resizing
    }

    void Shift(int start, int delta) {
        if (delta < 0) start -= delta;
        if (start < size)
            memmove(items->m_Items + start + delta,
                    items->m_Items + start,
                    (size - start) * sizeof(T));
        size += delta;
        if (delta < 0)
            memset(items->m_Items + size + delta, 0, -delta * sizeof(T));
    }

    inline constexpr List() : Il2CppObject() {}
};

    /**
        @brief System.Collections.Generic.Dictionary<TKey, TValue> layout
               (non-DOTNET35 / .NET 4.x runtime)
        @tparam TKey   Key type
        @tparam TValue Value type

        Read operations (TryGet, Get, Contains*, ToMap, GetKeys, GetValues)
        are implemented by directly scanning the entries array.

        Write operations (Add, Insert, Remove, Clear) call the managed method
        through a user-supplied function pointer; set those pointers before use
        if you need mutation. If you only read, ignore them.
    */
    template<typename TKey, typename TValue>
    struct Dictionary : Il2CppObject {

        // -----------------------------------------------------------------
        // Internal entry layout  (mirrors System.Collections.Generic.Entry)
        // -----------------------------------------------------------------
        struct Entry {
            int    hashCode{}; // -1 = free / deleted slot
            int    next{};
            TKey   key{};
            TValue value{};
        };

        // -----------------------------------------------------------------
        // Fields  (must match the runtime layout exactly)
        // -----------------------------------------------------------------
        Array<int>   *buckets{};
        Array<Entry> *entries{};
        int   count{};       // entries used (including deleted slots)
        int   version{};
        int   freeList{};
        int   freeCount{};
        void *comparer{};
        Array<TKey>   *keys{};
        Array<TValue> *values{};
        void *syncRoot{};

        // -----------------------------------------------------------------
        // Accessors
        // -----------------------------------------------------------------

        /** @return Number of live key-value pairs (excludes deleted slots). */
        [[nodiscard]] int GetSize()    const { return count - freeCount; }

        /** @return Internal version counter (incremented on each mutation). */
        [[nodiscard]] int GetVersion() const { return version; }

        // -----------------------------------------------------------------
        // Conversion helpers
        // -----------------------------------------------------------------

        /**
            @brief Convert to std::map.
            Skips deleted / free slots (hashCode < 0).
            @return Map of all live entries.
        */
        std::map<TKey, TValue> ToMap() const {
            std::map<TKey, TValue> ret{};
            if (!this || !entries) return ret;
            for (int i = 0; i < count; ++i) {
                const Entry &e = entries->m_Items[i];
                if (e.hashCode >= 0)
                    ret.emplace(e.key, e.value);
            }
            return ret;
        }

        /**
            @brief Get all live keys.
            @return Vector of keys, skipping deleted slots.
        */
        std::vector<TKey> GetKeys() const {
            std::vector<TKey> ret{};
            if (!this || !entries) return ret;
            for (int i = 0; i < count; ++i)
                if (entries->m_Items[i].hashCode >= 0)
                    ret.emplace_back(entries->m_Items[i].key);
            return ret;
        }

        /**
            @brief Get all live values.
            @return Vector of values, skipping deleted slots.
        */
        std::vector<TValue> GetValues() const {
            std::vector<TValue> ret{};
            if (!this || !entries) return ret;
            for (int i = 0; i < count; ++i)
                if (entries->m_Items[i].hashCode >= 0)
                    ret.emplace_back(entries->m_Items[i].value);
            return ret;
        }

        // -----------------------------------------------------------------
        // Read operations (no il2cpp required)
        // -----------------------------------------------------------------

        /**
            @brief Try to find a value by key.
            Uses linear scan over entries; skips deleted slots.
            @param key   Key to look up
            @param value Out-pointer that receives the found value
            @return True if key was found.
        */
        bool TryGet(TKey key, TValue *value) const {
            if (!this || !entries) return false;
            for (int i = 0; i < count; ++i) {
                const Entry &e = entries->m_Items[i];
                if (e.hashCode >= 0 && e.key == key) {
                    if (value) *value = e.value;
                    return true;
                }
            }
            return false;
        }

        /**
            @brief Get value by key.
            @return Value if found, otherwise default-constructed TValue.
        */
        TValue Get(TKey key) const {
            TValue ret{};
            TryGet(key, &ret);
            return ret;
        }

        /** Alias of Get(). */
        TValue operator[](TKey key) const { return Get(key); }

        /**
            @brief Check whether a key exists.
            @return True if key is present.
        */
        bool ContainsKey(TKey key) const {
            return TryGet(key, nullptr);
        }

        /**
            @brief Check whether a value exists (linear scan).
            @return True if value is present.
        */
        bool ContainsValue(TValue value) const {
            if (!this || !entries) return false;
            for (int i = 0; i < count; ++i) {
                const Entry &e = entries->m_Items[i];
                if (e.hashCode >= 0 && e.value == value) return true;
            }
            return false;
        }

        // -----------------------------------------------------------------
        // Write operations
        // Provide function pointers before calling, or call through il2cpp
        // in your own wrapper.
        // -----------------------------------------------------------------

        /**
            @brief Optional: point these at the managed methods via your own
            il2cpp helper before calling Add / Insert / Remove / Clear.
        */
        using FnAdd    = void (*)(void* self, TKey key, TValue val);
        using FnInsert = void (*)(void* self, TKey key, TValue val); // set_Item
        using FnRemove = bool (*)(void* self, TKey key);
        using FnClear  = void (*)(void* self);

        static FnAdd    fnAdd;
        static FnInsert fnInsert;
        static FnRemove fnRemove;
        static FnClear  fnClear;

        void Add(TKey key, TValue value) {
            if (fnAdd) fnAdd(this, key, value);
        }

        void Insert(TKey key, TValue value) {
            if (fnInsert) fnInsert(this, key, value);
        }

        bool Remove(TKey key) {
            if (fnRemove) return fnRemove(this, key);
            return false;
        }

        void Clear() {
            if (fnClear) fnClear(this);
        }

        inline constexpr Dictionary() : Il2CppObject() {}
    };

    // Static function-pointer definitions (define in one .cpp)
    template<typename TKey, typename TValue>
    typename Dictionary<TKey, TValue>::FnAdd    Dictionary<TKey, TValue>::fnAdd    = nullptr;

    template<typename TKey, typename TValue>
    typename Dictionary<TKey, TValue>::FnInsert Dictionary<TKey, TValue>::fnInsert = nullptr;

    template<typename TKey, typename TValue>
    typename Dictionary<TKey, TValue>::FnRemove Dictionary<TKey, TValue>::fnRemove = nullptr;

    template<typename TKey, typename TValue>
    typename Dictionary<TKey, TValue>::FnClear  Dictionary<TKey, TValue>::fnClear  = nullptr;

} // namespace Unity
