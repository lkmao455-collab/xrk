#pragma once

#include <QMutex>
#include <QWaitCondition>
#include <queue>

namespace xrk {

template<typename T>
class FrameQueue {
public:
    explicit FrameQueue(int maxSize = 3);
    ~FrameQueue();

    void enqueue(const T& frame);
    bool enqueueNonBlocking(const T& frame);  // Returns false if queue full (no wait)
    T dequeue(int timeoutMs = 100);
    void clear();
    int size() const;
    int capacity() const;
    bool isEmpty() const;

private:
    mutable QMutex m_mutex;
    QWaitCondition m_notEmpty;
    QWaitCondition m_notFull;
    std::queue<T> m_queue;
    int m_maxSize;
    bool m_shutdown;
};

template<typename T>
FrameQueue<T>::FrameQueue(int maxSize)
    : m_maxSize(maxSize), m_shutdown(false) {
}

template<typename T>
FrameQueue<T>::~FrameQueue() {
    clear();
}

template<typename T>
void FrameQueue<T>::enqueue(const T& frame) {
    QMutexLocker locker(&m_mutex);

    while (m_queue.size() >= m_maxSize && !m_shutdown) {
        m_notFull.wait(&m_mutex);
    }

    if (m_shutdown) {
        return;
    }

    m_queue.push(frame);
    m_notEmpty.wakeOne();
}

template<typename T>
bool FrameQueue<T>::enqueueNonBlocking(const T& frame) {
    QMutexLocker locker(&m_mutex);
    if (m_shutdown || m_queue.size() >= m_maxSize) {
        return false;
    }
    m_queue.push(frame);
    m_notEmpty.wakeOne();
    return true;
}

template<typename T>
T FrameQueue<T>::dequeue(int timeoutMs) {
    QMutexLocker locker(&m_mutex);

    while (m_queue.empty() && !m_shutdown) {
        if (!m_notEmpty.wait(&m_mutex, timeoutMs)) {
            return T();
        }
    }

    if (m_shutdown && m_queue.empty()) {
        return T();
    }

    T frame = m_queue.front();
    m_queue.pop();
    m_notFull.wakeOne();
    return frame;
}

template<typename T>
void FrameQueue<T>::clear() {
    QMutexLocker locker(&m_mutex);
    while (!m_queue.empty()) {
        m_queue.pop();
    }
    m_notFull.wakeAll();
}

template<typename T>
int FrameQueue<T>::size() const {
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

template<typename T>
int FrameQueue<T>::capacity() const {
    QMutexLocker locker(&m_mutex);
    return m_maxSize;
}

template<typename T>
bool FrameQueue<T>::isEmpty() const {
    QMutexLocker locker(&m_mutex);
    return m_queue.empty();
}

} // namespace xrk
