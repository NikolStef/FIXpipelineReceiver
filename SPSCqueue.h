// SPSC queue + FIX message

#pragma once
#include <atomic>
#include <cstring>

template<typename T, size_t Capacity>
class SQueue {
	static_assert(((Capacity - 1)& Capacity) == 0, "capacity not power of 2");

public:
	bool enqueue(const T& item) {
		size_t head = head_.load(std::memory_order_relaxed);
		size_t next = (head + 1) & mask_;

		if (next == tail_.load(std::memory_order_acquire))
			return false;

		buffer_[head] = item;
		head_.store(next, std::memory_order_release);
		return true;
	}

	bool dequeue(T& out) {
		size_t tail = tail_.load(std::memory_order_relaxed);
		size_t next = (tail + 1) & mask_;

		if (tail == head_.load(std::memory_order_acquire))
			return false;

		out = buffer_[tail];
		tail_.store(next, std::memory_order_release);
		return true;
	}

private:
	static constexpr size_t mask_ = Capacity - 1;
	alignas(64) std::atomic<size_t> head_{ 0 };
	alignas(64) std::atomic<size_t> tail_{ 0 };
	T buffer_[Capacity];
};

struct FixMessage {
	char data[512];
	size_t len;
};
