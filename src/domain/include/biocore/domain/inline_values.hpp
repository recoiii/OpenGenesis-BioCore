#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace biocore::domain {

template <typename T, std::size_t InlineCapacity>
class InlineValues final {
    static_assert(InlineCapacity > 0U);

public:
    using value_type = T;

    InlineValues() = default;

    void clear() noexcept {
        heap_.clear();
        size_ = 0U;
    }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool uses_heap_storage() const noexcept { return !heap_.empty(); }

    void push_back(const T& value) {
        ensure_capacity_for_one_more();
        if (heap_.empty()) {
            inline_[size_] = value;
        } else {
            heap_.push_back(value);
        }
        ++size_;
    }

    void push_back(T&& value) {
        ensure_capacity_for_one_more();
        if (heap_.empty()) {
            inline_[size_] = std::move(value);
        } else {
            heap_.push_back(std::move(value));
        }
        ++size_;
    }

    template <typename... Args>
    T& emplace_back(Args&&... args) {
        ensure_capacity_for_one_more();
        if (heap_.empty()) {
            inline_[size_] = T(std::forward<Args>(args)...);
            ++size_;
            return inline_[size_ - 1U];
        }
        heap_.emplace_back(std::forward<Args>(args)...);
        ++size_;
        return heap_.back();
    }

    [[nodiscard]] const T& operator[](const std::size_t index) const noexcept {
        return heap_.empty() ? inline_[index] : heap_[index];
    }

    [[nodiscard]] T& operator[](const std::size_t index) noexcept {
        return heap_.empty() ? inline_[index] : heap_[index];
    }

    [[nodiscard]] std::span<const T> values() const noexcept {
        const T* data = heap_.empty() ? inline_.data() : heap_.data();
        return {data, size_};
    }

    [[nodiscard]] std::span<T> values() noexcept {
        T* data = heap_.empty() ? inline_.data() : heap_.data();
        return {data, size_};
    }

private:
    void ensure_capacity_for_one_more() {
        if (!heap_.empty() || size_ < InlineCapacity) {
            return;
        }
        heap_.reserve(InlineCapacity * 2U);
        for (std::size_t index = 0U; index < size_; ++index) {
            heap_.push_back(std::move(inline_[index]));
        }
    }

    std::array<T, InlineCapacity> inline_{};
    std::vector<T> heap_{};
    std::size_t size_{0U};
};

}  // namespace biocore::domain
