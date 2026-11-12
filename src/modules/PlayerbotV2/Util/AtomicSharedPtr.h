// AtomicSharedPtr<T> - std::atomic<std::shared_ptr<T>> where the standard library has it (C++20, MSVC and libstdc++),
// otherwise the same load/store interface over the C++11 std::atomic_load/store shared_ptr functions. Apple's libc++
// does not provide the std::atomic<std::shared_ptr<T>> specialization yet (std::atomic requires a trivially copyable T).

#pragma once

#include <atomic>
#include <memory>

namespace Playerbot {

#if defined(__cpp_lib_atomic_shared_ptr)

template <class T>
using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;

#else

template <class T>
class AtomicSharedPtr
{
public:
    AtomicSharedPtr() = default;
    AtomicSharedPtr(AtomicSharedPtr const&) = delete;
    AtomicSharedPtr& operator=(AtomicSharedPtr const&) = delete;

    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const
    {
        return std::atomic_load_explicit(&ptr_, order);
    }

    void store(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst)
    {
        std::atomic_store_explicit(&ptr_, std::move(value), order);
    }

private:
    std::shared_ptr<T> ptr_;
};

#endif

} // namespace Playerbot
