#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <unordered_map>
#include <utility>

// LruCache<Key, Value>
//
// Простой LRU-кэш. Не thread-safe by design: caller держит mutex.
// Причина: используется в hot-path (ChannelDataModel::GetValueByRow),
// где дополнительная синхронизация внутри была бы лишней.
//
// onEvict вызывается при:
//   * вытеснении по лимиту (EnforceLimit в Put / SetMaxSize)
//   * Erase(key)
//   * Clear()
// Не вызывается при обновлении существующего ключа (Put поверх).
//
// SetMaxSize(0) → unlimited (без вытеснения).
//
// Пример:
//   LruCache<std::string, int> c(200);
//   c.onEvict = [](const std::string &k, const int &v) { /* ... */ };
//   if (int *v = c.Get("k")) { /* hit */ }
//   c.Put("k", 42);
template <class Key, class Value> class LruCache {
public:
  using EvictFn = std::function<void(const Key &, const Value &)>;
  EvictFn onEvict;

  explicit LruCache(size_t maxSize = 256) : m_maxSize(maxSize) {}

  // Hit — перемещает key в MRU, возвращает указатель на value.
  // Miss — nullptr.
  // Указатель живёт до следующей мутации кэша.
  Value *Get(const Key &key) {
    auto it = m_map.find(key);
    if (it == m_map.end())
      return nullptr;
    m_list.splice(m_list.begin(), m_list, it->second.second);
    return &it->second.first;
  }

  // Hit без обновления LRU-позиции. Указатель живёт до следующей мутации.
  const Value *Peek(const Key &key) const {
    auto it = m_map.find(key);
    return it == m_map.end() ? nullptr : &it->second.first;
  }

  bool Contains(const Key &key) const { return m_map.count(key) != 0; }

  // Вставка или обновление. Существующий ключ — обновляет value и
  // перемещает в MRU. Новый ключ — вставляет, затем EnforceLimit.
  void Put(const Key &key, Value v) {
    auto it = m_map.find(key);
    if (it != m_map.end()) {
      it->second.first = std::move(v);
      m_list.splice(m_list.begin(), m_list, it->second.second);
      return;
    }
    m_list.push_front(key);
    m_map.emplace(key, std::make_pair(std::move(v), m_list.begin()));
    EnforceLimit();
  }

  // Удаляет один ключ с вызовом onEvict.
  void Erase(const Key &key) {
    auto it = m_map.find(key);
    if (it == m_map.end())
      return;
    if (onEvict)
      onEvict(it->first, it->second.first);
    m_list.erase(it->second.second);
    m_map.erase(it);
  }

  // 0 = unlimited. Немедленно вытесняет лишнее с вызовом onEvict.
  void SetMaxSize(size_t maxSize) {
    m_maxSize = maxSize;
    EnforceLimit();
  }

  size_t MaxSize() const { return m_maxSize; }
  size_t Size() const { return m_map.size(); }
  bool Empty() const { return m_map.empty(); }

  // Удаляет все элементы с вызовом onEvict для каждого.
  void Clear() {
    if (onEvict) {
      for (const auto &kv : m_map)
        onEvict(kv.first, kv.second.first);
    }
    m_list.clear();
    m_map.clear();
  }

private:
  using ListIt = typename std::list<Key>::iterator;
  using Entry = std::pair<Value, ListIt>;

  void EnforceLimit() {
    if (m_maxSize == 0)
      return;
    while (m_map.size() > m_maxSize && !m_list.empty()) {
      const Key &oldest = m_list.back();
      auto it = m_map.find(oldest);
      if (it == m_map.end()) {
        m_list.pop_back();
        continue;
      }
      if (onEvict)
        onEvict(it->first, it->second.first);
      m_list.pop_back();
      m_map.erase(it);
    }
  }

  size_t m_maxSize;
  std::list<Key> m_list; // MRU at head, LRU at tail
  std::unordered_map<Key, Entry> m_map;
};
