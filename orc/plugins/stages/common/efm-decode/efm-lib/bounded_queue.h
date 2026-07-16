/*
 * File:        bounded_queue.h
 * Purpose:     EFM-library - bounded blocking queue for pipeline hand-off
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef EFM_BOUNDED_QUEUE_H
#define EFM_BOUNDED_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

// A fixed-capacity single-producer / single-consumer blocking queue used to
// hand decoded work items (F2 sections) from the EFM front-end thread to the
// back-end thread (P-9). The bound provides back-pressure so a fast producer
// cannot grow the hand-off buffer without limit.
//
// Thread-safety: all public methods are safe to call concurrently. The intended
// use is exactly one producer (calling push) and one consumer (calling pop),
// with either side allowed to call close()/abort() from any thread.
template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : m_capacity(capacity) {}

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  // Move |item| into the queue, blocking while the queue is full. Returns true
  // once the item is enqueued; returns false without enqueuing if the queue has
  // been closed or aborted (the item is left untouched by the caller's move).
  bool push(T&& item) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_notFull.wait(lock, [this] {
      return m_queue.size() < m_capacity || m_closed || m_aborted;
    });
    if (m_closed || m_aborted) return false;
    m_queue.push(std::move(item));
    lock.unlock();
    m_notEmpty.notify_one();
    return true;
  }

  // Move the next item into |out|, blocking while the queue is empty. Returns
  // true on success; returns false when the queue has been drained after
  // close(), or immediately if the queue has been aborted (any still-queued
  // items are discarded on abort).
  bool pop(T& out) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_notEmpty.wait(
        lock, [this] { return !m_queue.empty() || m_closed || m_aborted; });
    if (m_aborted) return false;
    if (m_queue.empty()) return false;  // implies m_closed
    out = std::move(m_queue.front());
    m_queue.pop();
    lock.unlock();
    m_notFull.notify_one();
    return true;
  }

  // Signal that no further items will be pushed. A consumer blocked in pop()
  // drains the remaining items and then observes end-of-stream (pop() ->
  // false).
  void close() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_closed = true;
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();
  }

  // Abandon the queue: wake any blocked producer and consumer immediately.
  // Subsequent push() calls return false and pop() returns false without
  // delivering the remaining items. Used for error / cancellation tear-down.
  void abort() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_aborted = true;
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();
  }

 private:
  const std::size_t m_capacity;
  std::mutex m_mutex;
  std::condition_variable m_notFull;
  std::condition_variable m_notEmpty;
  std::queue<T> m_queue;
  bool m_closed = false;
  bool m_aborted = false;
};

#endif  // EFM_BOUNDED_QUEUE_H
