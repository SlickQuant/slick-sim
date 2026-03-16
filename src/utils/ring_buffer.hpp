#pragma once

#include <cassert>

namespace slick::sim::utils {

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : capacity_(capacity)
        , buffer_(new T[capacity])
        , head_(0)
        , tail_(0)
        , mask_(capacity_ - 1)
    {
        assert((capacity & (capacity - 1)) == 0 && "Capacity must be a power of two");
    }

    ~RingBuffer() {
        delete[] buffer_;
        buffer_ = nullptr;
    }

    void push(const T& item) {
        buffer_[head_++ & mask_] = item;
        if ((head_ & mask_) == (tail_ & mask_)) {
            // Overwrite oldest item
            ++tail_;
        }
    }

    void emplace(T&& item) {
        buffer_[head_++ & mask_] = std::move(item);
        if ((head_ & mask_) == (tail_ & mask_)) {
            // Overwrite oldest item
            ++tail_;
        }
    }

    bool try_push(const T& item) {
        if (size() == capacity_) {
            return false; // Buffer is full
        }
        buffer_[head_++ & mask_] = item;
        return true;
    }

    bool try_emplace(T&& item) {
        if (size() == capacity_) {
            return false; // Buffer is full
        }
        buffer_[head_++ & mask_] = std::move(item);
        return true;
    }

    bool pop_front(T& item) {
        if (isEmpty()) {
            return false; // Buffer is empty
        }
        item = buffer_[tail_++ & mask_];
        return true;
    }

    bool pop_back(T& item) {
        if (isEmpty()) {
            return false; // Buffer is empty
        }
        item = buffer_[--head_ & mask_];
        return true;
    }

    T& front() {
        return buffer_[tail_ & mask_];
    }

    T& back() {
        return buffer_[(head_ - 1) & mask_];
    }

    size_t size() const {
        return head_ - tail_;
    }

    bool isEmpty() const {
        return head_ == tail_;
    }

    bool isFull() const {
        return size() == capacity_;
    }

    T* operator[](size_t index) {
        if (index >= size()) {
            return nullptr;
        }
        return &buffer_[(tail_ + index) & mask_];
    }

private:
    size_t capacity_;
    T* buffer_;
    size_t head_;
    size_t tail_;
    size_t mask_;
};

}