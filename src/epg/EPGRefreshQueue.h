#pragma once

#include <deque>
#include <functional>
#include <mutex>

// Последовательная очередь задач обновления EPG-источников.
//
// Гарантии:
//   * В каждый момент выполняется не более одной задачи.
//   * Все Enqueue-задачи будут выполнены в порядке добавления,
//     если не был вызван Clear().
//   * Clear() очищает только ОЖИДАЮЩИЕ задачи; текущая доиграет.
//
// Потоки:
//   * Enqueue() / OnTaskFinished() — из UI-потока.
//   * Clear() — из любого потока.
class EPGRefreshQueue {
public:
  using Task = std::function<void()>;

  void Enqueue(Task task) {
    Task toRun;
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      if (m_running || !m_queue.empty()) {
        m_queue.push_back(std::move(task));
        return;
      }
      m_running = true;
      toRun = std::move(task);
    }
    toRun();
  }

  void OnTaskFinished() {
    Task toRun;
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      if (m_queue.empty()) {
        m_running = false;
        return;
      }
      toRun = std::move(m_queue.front());
      m_queue.pop_front();
    }
    toRun();
  }

  void Clear() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_queue.clear();
  }

  bool IsEmpty() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_queue.empty() && !m_running;
  }

  size_t Size() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_queue.size() + (m_running ? 1u : 0u);
  }

private:
  mutable std::mutex m_mutex;
  std::deque<Task> m_queue;
  bool m_running = false;
};
