#pragma once

#include <stdexcept>
#include <initializer_list>
#include <array>
#include <utility>
#include <type_traits>
#include <mutex>
#include <condition_variable>
#include <cstddef>

template <typename ValueType, std::size_t Capacity> requires (Capacity > 0)
class ring_buffer
{
public:

	ring_buffer()
		: m_readIndex(0), m_writeIndex(0), m_size(0)
	{
	}

	ring_buffer(std::initializer_list<ValueType> data)
		: ring_buffer{}
	{
		for (const ValueType& value : data)
			push(value);
	}

	void push(const ValueType& value)
	{
		std::lock_guard<std::mutex> lck{ m_mtx };

		m_data[m_writeIndex] = value;
		m_writeIndex = (m_writeIndex + 1) % Capacity;

		if (m_size == Capacity)
			m_readIndex = (m_readIndex + 1) % Capacity;
		else
			++m_size;

		m_nonEmptyCv.notify_one();
	}

	ValueType blocking_pop()
	{
		std::unique_lock<std::mutex> lck{ m_mtx };
		m_nonEmptyCv.wait(lck, [this]() {return m_size > 0; });

		const ValueType output{ m_data[m_readIndex] };
		m_readIndex = (m_readIndex + 1) % Capacity;
		--m_size;

		return output;
	}

	ValueType blocking_front() const
	{
		std::unique_lock<std::mutex> lck{ m_mtx };
		m_nonEmptyCv.wait(lck, [this]() {return m_size > 0; });
		return m_data[m_readIndex];
	}

	ValueType blocking_back() const
	{
		std::unique_lock<std::mutex> lck{ m_mtx };
		m_nonEmptyCv.wait(lck, [this]() {return m_size > 0; });
		return m_data[(m_readIndex + (m_size - 1)) % Capacity];
	}

	ValueType get_by_value(std::size_t index) const
	{
		std::lock_guard<std::mutex> lck{ m_mtx };

		if (index >= m_size)
			throw std::out_of_range{ "Out of bounds" };

		return m_data[(m_readIndex + index) % Capacity];
	}

	void clear()
	{
		std::lock_guard<std::mutex> lck{ m_mtx };
		m_readIndex = m_writeIndex = m_size = 0;
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lck{ m_mtx };
		return m_size == 0;
	}

	bool full() const
	{
		std::lock_guard<std::mutex> lck{ m_mtx };
		return m_size == Capacity;
	}

	std::size_t size() const
	{
		std::lock_guard<std::mutex> lck{ m_mtx };
		return m_size;
	}

	static constexpr std::size_t capacity() noexcept
	{
		return Capacity;
	}

private:

	mutable std::mutex m_mtx;
	mutable std::condition_variable m_nonEmptyCv;
	std::array<ValueType, Capacity> m_data;
	std::size_t m_readIndex,
		m_writeIndex,
		m_size;
};