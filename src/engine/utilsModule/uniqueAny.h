#pragma once
#include <functional>
#include <utility>

namespace IKIGAI::UTILS {
    namespace detail {
        constexpr inline std::size_t small_buffer_size = 3 * sizeof(void*);
        constexpr inline std::size_t small_buffer_alignment = std::alignment_of_v<void*>;
        using small_buffer = std::aligned_storage_t<small_buffer_size, small_buffer_alignment>;

        template <typename T>
        constexpr inline bool is_small_object_v = sizeof(T) <= sizeof(small_buffer) && 
            std::alignment_of_v<small_buffer> % std::alignment_of_v<T> == 0 &&
            std::is_nothrow_move_constructible_v<T>;

        union storage {
            constexpr storage() : ptr(nullptr) {}
            void* ptr;
            small_buffer buf;
        };

        struct vtable_type {
            void(&destroy)(storage&);
            void(&move)(storage&, storage&);
            void* (&get)(storage&);
            const std::type_info& typeinfo;
        };

        template <class T>
        struct small_buffer_handler;
        template <class T>
        struct default_handler;

        template <class T>
        using handler = std::conditional_t<is_small_object_v<T>, small_buffer_handler<T>, default_handler<T>>;

        template <typename T>
        struct is_in_place_type : std::false_type {};
        template <typename T>
        struct is_in_place_type<std::in_place_type_t<T>> : std::true_type {};
        template <typename T>
        inline constexpr bool is_in_place_type_v = is_in_place_type<T>::value;
    } // namespace detail

    class unique_any {
    public:
        ///////////////////////////////////////////////////////////////////////////
        // Constructors
        // https://en.cppreference.com/w/cpp/utility/any/any (1)
        constexpr unique_any() noexcept : vtable_(nullptr) {}
        // https://en.cppreference.com/w/cpp/utility/any/any (2)
        unique_any(const unique_any& other) = delete;
        // https://en.cppreference.com/w/cpp/utility/any/any (3)
        unique_any(unique_any&& other) noexcept {
            if (other.vtable_ != nullptr) {
                other.vtable_->move(other.storage_, storage_);
                vtable_ = std::exchange(other.vtable_, nullptr);
            } else {
                vtable_ = nullptr;
            }
        }
        // https://en.cppreference.com/w/cpp/utility/any/any (4)
        template <class ValueType, class T = std::decay_t<ValueType>,
            class = std::enable_if_t<!std::is_same_v<T, unique_any> && !detail::is_in_place_type_v<T>>>
        unique_any(ValueType&& value) : vtable_(&detail::handler<T>::vtable) {
            detail::handler<T>::create(storage_, std::forward<ValueType>(value));
        }
        // https://en.cppreference.com/w/cpp/utility/any/any (5)
        template <class ValueType, class... Args, class T = std::decay_t<ValueType>,
            class = std::enable_if_t<std::is_constructible_v<T, Args...>>>
        explicit unique_any(std::in_place_type_t<ValueType> /*unused*/, Args &&...args)
            : vtable_(&detail::handler<T>::vtable) {
            detail::handler<T>::create(storage_, std::forward<Args>(args)...);
        }
        // https://en.cppreference.com/w/cpp/utility/any/any (6)
        template <class ValueType, class U, class... Args, class T = std::decay_t<ValueType>,
            class = std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args...>>>
        explicit unique_any(std::in_place_type_t<ValueType> /*unused*/, std::initializer_list<U> il, Args &&...args)
            : vtable_(&detail::handler<T>::vtable) {
            detail::handler<T>::create(storage_, il, std::forward<Args>(args)...);
        }

        ///////////////////////////////////////////////////////////////////////////
        // Assignment operators
        // https://en.cppreference.com/w/cpp/utility/any/operator%3D (1)
        auto operator=(const unique_any& rhs)->unique_any & = delete;
        // https://en.cppreference.com/w/cpp/utility/any/operator%3D (2)
        auto operator=(unique_any&& rhs) noexcept -> unique_any& {
            unique_any(std::move(rhs)).swap(*this);
            return *this;
        }
        // https://en.cppreference.com/w/cpp/utility/any/operator%3D (3)
        template <typename ValueType, class T = std::decay_t<ValueType>,
            class = std::enable_if_t<!std::is_same_v<T, unique_any>>>
        auto operator=(ValueType&& rhs) -> unique_any& {
            unique_any(std::forward<ValueType>(rhs)).swap(*this);
            return *this;
        }

        ///////////////////////////////////////////////////////////////////////////
        // Destructor
        // https://en.cppreference.com/w/cpp/utility/any/~any
        ~unique_any() {
            if (vtable_ != nullptr) {
                vtable_->destroy(storage_);
            }
        }

        ///////////////////////////////////////////////////////////////////////////
        // Modifiers
        // https://en.cppreference.com/w/cpp/utility/any/emplace (1)
        template <class ValueType, class... Args, class T = std::decay_t<ValueType>,
            class = std::enable_if_t<std::is_constructible_v<T, Args...>>>
        auto emplace(Args &&...args) -> T& {
            if (vtable_ != nullptr) {
                vtable_->destroy(storage_);
            }
            vtable_ = &detail::handler<T>::vtable;
            return detail::handler<T>::create(storage_, std::forward<Args>(args)...);
        }
        // https://en.cppreference.com/w/cpp/utility/any/emplace (2)
        template <class ValueType, class U, class... Args, class T = std::decay_t<ValueType>,
            class = std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args...>>>
        auto emplace(std::initializer_list<U> il, Args &&...args) -> T& {
            if (vtable_ != nullptr) {
                vtable_->destroy(storage_);
            }
            vtable_ = &detail::handler<T>::vtable;
            return detail::handler<T>::create(storage_, il, std::forward<Args>(args)...);
        }
        // https://en.cppreference.com/w/cpp/utility/any/reset
        void reset() noexcept {
            if (vtable_ != nullptr) {
                vtable_->destroy(storage_);
                vtable_ = nullptr;
            }
        }
        // https://en.cppreference.com/w/cpp/utility/any/swap
        void swap(unique_any& other) noexcept {
            if (this == &other) {
                return;
            }
            if (vtable_ == nullptr && other.vtable_ == nullptr) {
                return;
            }
            if (vtable_ != nullptr && other.vtable_ != nullptr) {
                auto tmp = detail::storage();
                other.vtable_->move(other.storage_, tmp);
                vtable_->move(storage_, other.storage_);
                other.vtable_->move(tmp, storage_);
            } else if (vtable_ != nullptr) {
                vtable_->move(storage_, other.storage_);
            } else if (other.vtable_ != nullptr) {
                other.vtable_->move(other.storage_, storage_);
            }
            std::swap(vtable_, other.vtable_);
        }

        ///////////////////////////////////////////////////////////////////////////
        // Observers
        // https://en.cppreference.com/w/cpp/utility/any/has_value
        [[nodiscard]] auto has_value() const noexcept -> bool { return vtable_ != nullptr; }
        // https://en.cppreference.com/w/cpp/utility/any/type
        [[nodiscard]] auto type() const noexcept -> const std::type_info& {
            return vtable_ != nullptr ? vtable_->typeinfo : typeid(void);
        }

    private:
        template <typename T>
        auto unsafe_cast() -> T* {
            return static_cast<T*>(vtable_->get(storage_));
        }

        template <typename T>
        auto unsafe_cast() const -> const T* {
            return const_cast<unique_any*>(this)->unsafe_cast<T>();
        }

        template <typename T>
        friend auto any_cast(const unique_any* operand) noexcept -> const T*;

        template <typename T>
        friend auto any_cast(unique_any* operand) noexcept -> T*;

        const detail::vtable_type* vtable_;
        detail::storage storage_;
    };

    namespace detail {
        template <class T>
        struct small_buffer_handler {
        private:
            using allocator = std::allocator<T>;
            using allocator_traits = std::allocator_traits<allocator>;
            static auto cast(storage& s) -> T* { return static_cast<T*>(static_cast<void*>(&s.buf)); }
            static void destroy(storage& s) {
                auto alloc = allocator{};
                allocator_traits::destroy(alloc, cast(s));
            }
            static void move(storage& src, storage& dst) {
                auto alloc = allocator{};
                allocator_traits::construct(alloc, cast(dst), std::move(*cast(src)));
                allocator_traits::destroy(alloc, cast(src));
            }
            static auto get(storage& s) -> void* { return cast(s); }

        public:
            static constexpr inline vtable_type vtable = {destroy, move, get, typeid(T)};

            template <class... Args>
            static auto create(storage& s, Args &&...args) -> T& {
                auto alloc = allocator{};
                auto* ret = cast(s);
                allocator_traits::construct(alloc, ret, std::forward<Args>(args)...);
                return *ret;
            }
        };

        template <typename Allocator, typename std::allocator_traits<Allocator>::size_type size>
        struct allocator_deleter {
            void operator()(typename std::allocator_traits<Allocator>::pointer p) noexcept {
                auto allocator = Allocator{};
                std::allocator_traits<Allocator>::deallocate(allocator, p, size);
            }
        };

        template <class T>
        struct default_handler {
        private:
            using allocator = std::allocator<T>;
            using allocator_traits = std::allocator_traits<allocator>;
            static void destroy(storage& s) {
                auto alloc = allocator{};
                auto* ptr = static_cast<T*>(s.ptr);
                allocator_traits::destroy(alloc, ptr);
                allocator_traits::deallocate(alloc, ptr, 1);
            }
            static void move(storage& src, storage& dst) { dst.ptr = src.ptr; }
            static auto get(storage& s) -> void* { return s.ptr; }

        public:
            static constexpr inline vtable_type vtable = {destroy, move, get, typeid(T)};

            template <class... Args>
            static auto create(storage& s, Args &&...args) -> T& {
                auto alloc = allocator{};
                auto holder = std::unique_ptr<T, allocator_deleter<allocator, 1>>(allocator_traits::allocate(alloc, 1));
                auto* ptr = holder.get();
                allocator_traits::construct(alloc, ptr, std::forward<Args>(args)...);
                s.ptr = holder.release();
                return *ptr;
            }
        };

    } // namespace detail

    // https://en.cppreference.com/w/cpp/utility/any/swap2
    inline void swap(unique_any& lhs, unique_any& rhs) noexcept {
        lhs.swap(rhs);
    }

    // https://en.cppreference.com/w/cpp/utility/any/any_cast (1)
    template <class T>
    auto any_cast(const unique_any& operand) -> T {
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        //static_assert(std::is_constructible_v<T, const U&>);
        if (auto ptr = any_cast<std::add_const_t<U>>(&operand)) {
            return static_cast<T>(*ptr);
        }
        throw std::bad_any_cast();
    }

    // https://en.cppreference.com/w/cpp/utility/any/any_cast (2)
    template <class T>
    auto any_cast(unique_any& operand) -> T {
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        //static_assert(std::is_constructible_v<T, U&>);
        if (auto ptr = any_cast<U>(&operand)) {
            return static_cast<T>(*ptr);
        }
        throw std::bad_any_cast();
    }

    // https://en.cppreference.com/w/cpp/utility/any/any_cast (3)
    template <class T>
    auto any_cast(unique_any&& operand) -> T {
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        //static_assert(std::is_constructible_v<T, U>);
        if (auto ptr = any_cast<U>(&operand)) {
            return static_cast<T>(std::move(*ptr));
        }
        throw std::bad_any_cast();
    }

    // https://en.cppreference.com/w/cpp/utility/any/any_cast (4)
    template <class T>
    auto any_cast(const unique_any* operand) noexcept -> const T* {
        static_assert(!std::is_reference_v<T>);
        if (operand && operand->type() == typeid(T)) {
            return operand->unsafe_cast<T>();
        }
        return nullptr;
    }

    // https://en.cppreference.com/w/cpp/utility/any/any_cast (5)
    template <class T>
    auto any_cast(unique_any* operand) noexcept -> T* {
        static_assert(!std::is_reference_v<T>);
        if (operand && operand->type() == typeid(T)) {
            return operand->unsafe_cast<T>();
        }
        return nullptr;
    }

    // https://en.cppreference.com/w/cpp/utility/any/make_any (1)
    template <class T, class... Args>
    auto make_unique_any(Args &&...args) -> unique_any {
        return unique_any(std::in_place_type<T>, std::forward<Args>(args)...);
    }

    // https://en.cppreference.com/w/cpp/utility/any/make_any (2)
    template <class T, class U, class... Args>
    auto make_unique_any(std::initializer_list<U> il, Args &&...args) -> unique_any {
        return unique_any(std::in_place_type<T>, il, std::forward<Args>(args)...);
    }
}
