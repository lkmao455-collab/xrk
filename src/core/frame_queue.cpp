#include "frame_queue.h"

namespace xrk {

FrameQueue::FrameQueue(int maxSize)
    : m_maxSize(maxSize), m_shutdown(false) {
}

FrameQueue::~FrameQueue() {
    fprintf(stderr, "[~FrameQueue] enter\n"); fflush(stderr);
    clear();
    fprintf(stderr, "[~FrameQueue] exit\n"); fflush(stderr);
}

void FrameQueue::enqueue(const QImage& frame) {
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

QImage FrameQueue::dequeue(int timeoutMs) {
    QMutexLocker locker(&m_mutex);

    while (m_queue.empty() && !m_shutdown) {
        if (!m_notEmpty.wait(&m_mutex, timeoutMs)) {
            return QImage();
        }
    }

    if (m_shutdown && m_queue.empty()) {
        return QImage();
    }

    QImage frame = m_queue.front();
    m_queue.pop();
    m_notFull.wakeOne();
    return frame;
}

void FrameQueue::clear() {
    QMutexLocker locker(&m_mutex);
    while (!m_queue.empty()) {
        m_queue.pop();
    }
    m_notFull.wakeAll();
}

int FrameQueue::size() const {
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

bool FrameQueue::isEmpty() const {
    QMutexLocker locker(&m_mutex);
    return m_queue.empty();
}

} // namespace xrk
