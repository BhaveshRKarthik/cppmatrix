#ifndef BHAVESH_CXX_VER

# ifdef _MSC_VER
#   define BHAVESH_CXX_VER _MSVC_LANG
# else
#   define BHAVESH_CXX_VER __cplusplus
# endif

# if BHAVESH_CXX_VER >= 202002L
#   define BHAVESH_CXX20_CONSTEXPR constexpr
#   define BHAVESH_CXX20 true
# else
#   define BHAVESH_CXX20_CONSTEXPR 
#   define BHAVESH_CXX20 false
# endif

# if BHAVESH_CXX_VER >= 201703L
#   define BHAVESH_CXX17_CONSTEXPR constexpr
# else
#   define BHAVESH_CXX17_CONSTEXPR 
# endif

#endif

#ifndef BHAVESH_DEBUG
# if !defined(BHAVESH_NO_DEDUCE_DEBUG) && (defined(_DEBUG) || !defined(NDEBUG))
# define BHAVESH_DEBUG
#   define BHAVESH_USE_IF_DEBUG(...) __VA_ARGS__
# else
#   define BHAVESH_USE_IF_DEBUG(...)
# endif
#endif

#if BHAVESH_CXX_VER < 201402L
# error "bhavesh_matrix.h needs atleast c++14"
#endif

#ifndef BHAVESH_MATRIX_H
#define BHAVESH_MATRIX_H 0.1

#if BHAVESH_CXX20
# define BHAVESH_CXX20_MATRIX_VIEW : public std::ranges::view_base
#else
# define BHAVESH_CXX20_MATRIX_VIEW
#endif

#include <cstddef> // size_t, ptrdiff_t
#include <utility> // std::forward
#include <memory> // placement new | std::construct_at | ...
#include <stdexcept> // exception types
#include <iterator> // iterator_tag
#include <complex> // complex numbers of fields are fields
#include <type_traits> // commonly used; enable_if...

#if BHAVESH_CXX20
#include <concepts> // used with ranges
#include <ranges>   // view_base
#include <compare>  // spaceship operator
#endif

namespace bhavesh {
	using std::size_t;
	using std::ptrdiff_t;

#ifndef BHAVESH_SILENCE_T
#define BHAVESH_SILENCE_T
	inline namespace detail {
		namespace silence_detail {
			enum silence_t : std::uint8_t {
				silence_none,
				silence_less,
				silence_greater,
				silence_both,
			};
			using silence_none_t    = std::integral_constant<silence_t, silence_t::silence_none>;
			using silence_less_t    = std::integral_constant<silence_t, silence_t::silence_less>;
			using silence_greater_t = std::integral_constant<silence_t, silence_t::silence_greater>;
			using silence_both_t    = std::integral_constant<silence_t, silence_t::silence_both>;
			
			template<silence_t sil>
			using silence_type = std::integral_constant<silence_t, sil>;
		}
	}

	constexpr auto silence_none    = silence_detail::silence_none_t{};
	constexpr auto silence_less    = silence_detail::silence_less_t{};
	constexpr auto silence_greater = silence_detail::silence_greater_t{};
	constexpr auto silence_both    = silence_detail::silence_both_t{};

#endif // !BHAVESH_SILENCE_T

	inline namespace detail {
	namespace matrix_detail {
		using namespace silence_detail;
		
		template <typename T>
		BHAVESH_CXX20_CONSTEXPR T* allocate(std::size_t count) {
			return std::allocator<T>{}.allocate(count);
		}
		template <typename T>
			BHAVESH_CXX20_CONSTEXPR void deallocate(T* ptr, std::size_t count) {
			std::allocator<T>{}.deallocate(ptr, count);
		}

#   if BHAVESH_CXX20
		using std::destroy_at;
		using std::construct_at;
		using std::destroy_n;
#   else
		template<typename T, class... Args>
		inline T* construct_at(T* p, Args&&... args) {
			return ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
		}
			
		template<typename T, std::enable_if_t<!std::is_array<T>::value, int> = 0>
		inline void destroy_at(T* p) {
			p->~T();
		}

		template<typename T, std::enable_if_t<std::is_array<T>::value, long> = 0>
		inline void destroy_at(T* p) {
			for (auto& elem : *p) (destroy_at)(std::addressof(elem));
		}

		template<class ForwardIt, class Size>
		ForwardIt destroy_n(ForwardIt first, Size n)
		{
			for (; n > 0; (void) ++first, --n)
				destroy_at(std::addressof(*first));
			return first;
		}
#   endif

		class incompletely_initialized : public std::runtime_error { using runtime_error::runtime_error; constexpr incompletely_initialized() = delete; };


		template<typename T>
		class _construction_holder {
		public:
			explicit BHAVESH_CXX20_CONSTEXPR _construction_holder()         : start(nullptr),          size(0), capacity(0) {}
			explicit BHAVESH_CXX20_CONSTEXPR _construction_holder(size_t s) : start((allocate<T>)(s)), size(0), capacity(s) {}

			BHAVESH_CXX20_CONSTEXPR ~_construction_holder() {
				if (start) {
					destroy_n(start, size);
					deallocate(start, capacity);
					start = nullptr;
					size = capacity = 0;
				}
			}

			template<typename ...tArgs>
			BHAVESH_CXX20_CONSTEXPR void emplace_back(tArgs&&... args) {
				(construct_at)(start + size++, std::forward<tArgs>(args)...);
			}

			template<bool suppress_check = (false BHAVESH_USE_IF_DEBUG(, true)), std::enable_if_t<suppress_check, long> = 0>
			BHAVESH_CXX20_CONSTEXPR T* release() {
				size = capacity = 0;
				return std::exchange(start, nullptr);
			}
			template<bool suppress_check = (false BHAVESH_USE_IF_DEBUG(, true)), std::enable_if_t<!suppress_check, int> = 0>
			BHAVESH_CXX20_CONSTEXPR T* release() {
				if (size != capacity) throw incompletely_initialized("Not all values were fully initialized; unsafe to keep uninitialized memory around.");
				size = capacity = 0;
				return std::exchange(start, nullptr);
			}
		
		private:
			T* start;
			size_t size;
			size_t capacity;
		};

		template<typename T>
		class _transpose_construction_holder {
		public:
			explicit BHAVESH_CXX20_CONSTEXPR _transpose_construction_holder() : start(nullptr), m(0), n(0) {}
			explicit BHAVESH_CXX20_CONSTEXPR _transpose_construction_holder(size_t m, size_t n) : start((allocate<T>)(m*n)), m(m), n(n) {}
			BHAVESH_CXX20_CONSTEXPR ~_transpose_construction_holder() {
				if (start) {
					for (int x = 0; x != i; ++i) {
						destroy_n(start + i * n, j + 1);
					}
					for (int x = i; x != m; ++i) {
						destroy_n(start + i * n, j);
					}
					deallocate(start, m*n);
					start = nullptr;
					i = j = m = n = 0;
				}
			}

			template<typename ...tArgs>
			BHAVESH_CXX20_CONSTEXPR void emplace_back(tArgs&&... args) {
				(construct_at)(start + i++ * n + j, std::forward<tArgs>(args)...);
				if (i == m) { i = 0; j++; }
			}

			template<bool suppress_check = (false BHAVESH_USE_IF_DEBUG(, true)), std::enable_if_t<suppress_check, long> = 0>
			BHAVESH_CXX20_CONSTEXPR T* release() {
				i = j = m = n = 0;
				return std::exchange(start, nullptr);
			}
			template<bool suppress_check = (false BHAVESH_USE_IF_DEBUG(, true)), std::enable_if_t<!suppress_check, int> = 0>
			BHAVESH_CXX20_CONSTEXPR T* release() {
				if (j != n) throw incompletely_initialized("Not all values were fully initialized; unsafe to keep uninitialized memory around.");
				i = j = m = n = 0;
				return std::exchange(start, nullptr);
			}

		private:
			T* start;
			size_t i=0, j=0;
			size_t m, n;
		};

		
		template<template<typename> typename _holder, typename T, std::enable_if_t<!std::is_trivially_default_constructible<T>::value, int> = 0>
		BHAVESH_CXX20_CONSTEXPR void emplace_default(_holder<T>& h) {
			h.emplace_back();
		}
		template<template<typename> typename _holder, typename T, std::enable_if_t<std::is_trivially_default_constructible<T>::value, long> = 0>
		BHAVESH_CXX20_CONSTEXPR void emplace_default(_holder<T>& h) {
			h.emplace_back(T());
		}

		template<typename T>
		BHAVESH_CXX20_CONSTEXPR inline T* _create_default_n(size_t s) {
			_construction_holder<T> h(s);
			for (size_t i = 0; i != s; ++i) {
				h.emplace_back();
			}
			return h.release<true>();
		}

		template<typename T, std::enable_if_t<!std::is_trivially_default_constructible<T>::value, int> = 0>
		BHAVESH_CXX20_CONSTEXPR inline T* create_default_n(size_t s) {
			return _create_default_n<T>(s);
		}

		template<typename T, std::enable_if_t<std::is_trivially_default_constructible<T>::value, long> = 0>
		BHAVESH_CXX20_CONSTEXPR inline T* create_default_n(size_t s) {
#		if BHAVESH_CXX20
			if (std::is_constant_evaluated()) {
				return _create_default_n(s);
			}
#		endif
			T* data = allocate<T>(s);
			std::memset(data, 0, sizeof(T) * s);
			return data;
		}


		template<typename T>
		BHAVESH_CXX20_CONSTEXPR inline T* create_fill_n(size_t s, const T& v) {
			_construction_holder<T> h(s);
			for (size_t i = 0; i != s; ++i) {
				h.emplace_back(v);
			}
			return h.release<true>();
		}

		template <silence_t sil>
		inline BHAVESH_CXX20_CONSTEXPR void throw_if_less(bool) noexcept {}
		template <silence_t sil, typename = std::enable_if_t<(sil & silence_t::silence_less) == 0>>
		inline BHAVESH_CXX20_CONSTEXPR void throw_if_less<sil>(bool check) noexcept(false) {
			if (condn) throw std::invalid_argument("too few arguments given to matrix(m, n, { ... })");
		}

		template <silence_t sil>
		inline BHAVESH_CXX20_CONSTEXPR void throw_if_more(bool) noexcept {}
		template <silence_t sil, typename = std::enable_if_t<(sil& silence_t::silence_greater) == 0>>
		inline BHAVESH_CXX20_CONSTEXPR void throw_if_more<sil>(bool check) noexcept(false) {
			if (condn) throw std::invalid_argument("too many arguments given to matrix(m, n, { ... })");
		}


		template<silence_t sil, typename T, std::enable_if_t<!std::is_trivial<T>::value, int> = 0>
		BHAVESH_CXX20_CONSTEXPR inline T* create_from_il(size_t s, std::initializer_list<T> il) {
			(throw_if_less<sil>)(il.size() < s);
			(throw_if_more<sil>)(il.size() > s);
			_construction_holder<T> h(s);
			const size_t x = std::min(il.size(), s);
			for (size_t i = 0; i != x; ++i) {
				h.emplace_back(il.begin()[i]);
			}
			for (size_t i = x; i < s; ++i) {
				h.emplace_back();
			}
			return h.release<true>();
		}

		template<silence_t sil, typename T, std::enable_if_t<std::is_trivial<T>::value, long> = 0>
		BHAVESH_CXX20_CONSTEXPR inline T* create_from_il(size_t s, std::initializer_list<T> il) {
			throw_if_less<sil>(il.size() < s);
			throw_if_more<sil>(il.size() > s);
#		if BHAVESH_CXX20
			if (std::is_constant_evaluated()) {
				_construction_holder<T> h(s);
				const size_t x = std::min(s, il.size());
				for (size_t i = 0; i != x; ++i) {
					h.emplace_back(il.begin()[i]);
				}
				for (size_t i = il.size(); i < s; ++i) {
					h.emplace_back();
				}
				return h.release<true>();
			}
#		endif
			T* data = allocate<T>(s);
			if (s >= il.size()) {
				std::memcpy((void*)data, il.begin(), s * sizeof(T));
			}
			else {
				std::memcpy((void*)data, il.begin(), il.size() * sizeof(T));
				std::memset((void*)(data + il.size()), 0, (s - il.size()) * sizeof(T));
			}
			return data;
		}

		template<silence_t sil, typename T>
		BHAVESH_CXX20_CONSTEXPR inline T* create_from_il_transpose(size_t m, size_t n, std::initializer_list<T> il) {
			const size_t s = m * n;
			throw_if<(sil & silence_t::silence_less)    == 0, std::invalid_argument>(il.size() < s,  "too few arguments given to matrix(transpose, m, n, { ... })");
			throw_if<(sil & silence_t::silence_greater) == 0, std::invalid_argument>(il.size() > s, "too many arguments given to matrix(transpose, m, n, { ... })");
			_transpose_construction_holder<T> h(m, n);
			const size_t x = std::min(s, il.size());
			for (size_t i = 0; i != x; ++i) {
				h.emplace_back(il.begin()[i]);
			}
			for (size_t i = il.size(); i < s; ++i) {
				emplace_default(h);
			}
			return h.release<true>();
		}


		template<typename T, std::enable_if_t<!std::is_trivial<T>::value, int> = 0>
		BHAVESH_CXX20_CONSTEXPR inline T* create_from_matrix(size_t s, const T* ptr) {
			_construction_holder<T> h(s);
			for (size_t i = 0; i != s; ++i) {
				h.emplace_back(ptr[i]);
			}
			return h.release<true>();
		}

		template<typename T, std::enable_if_t<std::is_trivial<T>::value, long> = 0>
		BHAVESH_CXX20_CONSTEXPR inline T* create_from_matrix(size_t s, const T* ptr) {
#			if BHAVESH_CXX20
				if (std::is_constant_evaluated()) {
					_construction_holder<T> h(s);
					for (size_t i = 0; i != s; ++i) {
						h.emplace_back(ptr[i]);
					}
					return h.release<true>();
				}
#			endif
			T* data = allocate<T>(s);
			memcpy(data, ptr, s * sizeof(T));
			return data;
		}

		template<typename T>
		BHAVESH_CXX20_CONSTEXPR inline T* create_from_matrix_transpose(size_t m, size_t n, const T* ptr) {
			const size_t s = m * n;
			_transpose_construction_holder<T> h(m, n);
			for (size_t i = 0; i != s; ++i) {
				h.emplace_back(ptr[i]);
			}
			return h.release<true>();
		}


#		if BHAVESH_CXX20
		/*  ranges based constructor  */
		template <typename R, typename T>
		concept container_compatible_range = std::ranges::input_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;

		template<silence_t sil, typename T, template <typename> typename _holder, std::ranges::input_range R>
		constexpr inline T* create_from_range(size_t s, R&& rng) requires std::convertible_to<std::ranges::range_reference_t<R>, T> {
			if constexpr (std::ranges::sized_range<R>) {
				const size_t x = std::ranges::size(rng);
				if constexpr ((sil & silence_t::silence_less   ) == 0) if (x < s) throw std::invalid_argument( "too few arguments given to matrix(transpose, m, n, { ... })");
				if constexpr ((sil & silence_t::silence_greater) == 0) if (x < s) throw std::invalid_argument("too many arguments given to matrix(transpose, m, n, { ... })");
				_holder<T> h(s);
				size_t i = 0;
				for (auto&& v : rng) {
					if (i >= s) break;
					h.emplace_back(v);
					i++;
				}
				for (; i < s; ++i) {
					emplace_default(h);
				}
				return h.release<true>();
			}
			else {
				_holder<T> h(s);
				size_t i = 0;
				for (auto&& v : rng) {
					if (i >= s) {
						if constexpr ((sil & silence_t::silence_greater) == 0) {
							throw std::invalid_argument("too many arguments given to matrix(transpose, m, n, { ... })");
						}
						break;
					}
					h.emplace_back(v);
					i++;
				}
				throw_if<(sil & silence_t::silence_less) == 0, std::invalid_argument>(i < s, "too few arguments given to matrix(transpose, m, n, { ... })");
				for (; i < s; ++i) {
					emplace_default(h);
				}
				return h.release<true>();
			}
		}

#		endif

		template<typename T, std::enable_if_t<!std::is_trivial<T>::value, int> = 0>
		BHAVESH_CXX20_CONSTEXPR inline void destroy_matrix(size_t s, T* data) {
			(destroy_n) (data, s);
			(deallocate)(data, s);
		}
		template<typename T, std::enable_if_t<std::is_trivial<T>::value, long> = 0>
		BHAVESH_CXX20_CONSTEXPR inline void destroy_matrix(size_t s, T* data) {
			(deallocate)(data, s);
		}

		template <typename T>
		constexpr bool is_field_v = std::is_floating_point_v<T>;

		template <typename T>
		constexpr bool is_field_v<std::complex<T>> = is_field_v<T>;

	} }

	inline namespace iterators {
	namespace matrix_iterators {

		template <typename T, typename _tag>
		class _row_iterator {
		public:
			using value_type = std::remove_cv_t<T>;
			using element_type = T;
			using difference_type = ptrdiff_t;
			using reference = T&;
			using const_reference = const T&;
			using pointer = T*;
			using const_pointer = const T*;
#if BHAVESH_CXX_VER >= 202002L
			using iterator_category = std::contiguous_iterator_tag;
			using iterator_concept = std::contiguous_iterator_tag;
#else
			using iterator_category = std::random_access_iterator_tag;
#endif
		public:
			_row_iterator() = default;
			_row_iterator(T* data) : m_data(data) {}
			_row_iterator(const _row_iterator&) = default;
			_row_iterator(_row_iterator&&) = default;
			_row_iterator& operator=(const _row_iterator&) = default;
			_row_iterator& operator=(_row_iterator&&) = default;

			template<typename=std::enable_if_t<!std::is_const<T>::value>>
			operator _row_iterator<const T, _tag>() {
				return _row_iterator<const T, _tag>(static_cast<const T*>(m_data));
			}

		public:
#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const _row_iterator& it) const { return m_data <=> it.m_data; }
			constexpr bool operator==(const _row_iterator&) const = default;
			constexpr bool operator!=(const _row_iterator&) const = default;
#else
			bool operator==(const _row_iterator& it) const { return m_data == it.m_data; }
			bool operator!=(const _row_iterator& it) const { return m_data != it.m_data; }
			bool operator< (const _row_iterator& it) const { return m_data <  it.m_data; }
			bool operator<=(const _row_iterator& it) const { return m_data <= it.m_data; }
			bool operator> (const _row_iterator& it) const { return m_data >  it.m_data; }
			bool operator>=(const _row_iterator& it) const { return m_data >= it.m_data; }
#endif

			BHAVESH_CXX20_CONSTEXPR element_type& operator*() const {
				return *m_data;
			}
			BHAVESH_CXX20_CONSTEXPR element_type* operator->() const {
				return m_data;
			}

			BHAVESH_CXX20_CONSTEXPR _row_iterator& operator++()    {                                              m_data++;    return *this; }
			BHAVESH_CXX20_CONSTEXPR _row_iterator  operator++(int) {               _row_iterator cpy = *this;     m_data++;    return cpy;   }
			BHAVESH_CXX20_CONSTEXPR _row_iterator& operator--()    {                                              m_data--;    return *this; }
			BHAVESH_CXX20_CONSTEXPR _row_iterator  operator--(int) {               _row_iterator cpy = *this;     m_data--;    return cpy;   }

			BHAVESH_CXX20_CONSTEXPR _row_iterator& operator+=(ptrdiff_t n) {                                      m_data += n; return *this; }
			BHAVESH_CXX20_CONSTEXPR _row_iterator& operator-=(ptrdiff_t n) {                                      m_data -= n; return *this; }

			BHAVESH_CXX20_CONSTEXPR _row_iterator  operator+ (ptrdiff_t n) const { _row_iterator cpy = *this; cpy.m_data += n; return cpy;   }
			BHAVESH_CXX20_CONSTEXPR _row_iterator  operator- (ptrdiff_t n) const { _row_iterator cpy = *this; cpy.m_data -= n; return cpy;   }

			BHAVESH_CXX20_CONSTEXPR ptrdiff_t      operator- (const _row_iterator& it) const { return m_data - it.m_data; }

			BHAVESH_CXX20_CONSTEXPR element_type&  operator[](ptrdiff_t n) const {
				return m_data[n];
			}
		private:
			T* m_data = nullptr;
		};

		template <typename T, typename _tag>
		BHAVESH_CXX20_CONSTEXPR _row_iterator<T, _tag> operator+(ptrdiff_t n, const _row_iterator<T, _tag>& it) {
			return it + n;
		}

		struct _row_iterator_tag {};
		struct _rowwise_iterator_tag {};
		
		template <typename T>
		using matrix_row_iterator = _row_iterator<T, _row_iterator_tag>;
		template <typename T>
		using matrix_rowmajor_iterator = _row_iterator<T, _rowwise_iterator_tag>;

		template <typename T>
		class matrix_column_iterator {
		public:
			using value_type = std::remove_cv_t<T>;
			using element_type = T;
			using difference_type = ptrdiff_t;
			using reference = T&;
			using const_reference = const T&;
			using pointer = T*;
			using const_pointer = const T*;
#if BHAVESH_CXX_VER >= 202002L
			using iterator_concept = std::random_access_iterator_tag;
#endif
			using iterator_category = std::random_access_iterator_tag;
		public:
			matrix_column_iterator() = default;
			matrix_column_iterator(size_t n, T* data) : n(n), m_data(data) {}
			matrix_column_iterator(const matrix_column_iterator&) = default;
			matrix_column_iterator(matrix_column_iterator&&) = default;
			matrix_column_iterator& operator=(const matrix_column_iterator&) = default;
			matrix_column_iterator& operator=(matrix_column_iterator&&) = default;

			template<typename=std::enable_if_t<!std::is_const<T>::value>>
			operator matrix_column_iterator<const T>() {
				return matrix_column_iterator<const T>(static_cast<const T*>(m_data));
			}

		public:
#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const matrix_column_iterator& it) const { return m_data <=> it.m_data; }
			constexpr bool operator==(const matrix_column_iterator&) const = default;
			constexpr bool operator!=(const matrix_column_iterator&) const = default;
#else
			bool operator==(const matrix_column_iterator& it) const { return m_data == it.m_data; }
			bool operator!=(const matrix_column_iterator& it) const { return m_data != it.m_data; }
			bool operator< (const matrix_column_iterator& it) const { return m_data <  it.m_data; }
			bool operator<=(const matrix_column_iterator& it) const { return m_data <= it.m_data; }
			bool operator> (const matrix_column_iterator& it) const { return m_data >  it.m_data; }
			bool operator>=(const matrix_column_iterator& it) const { return m_data >= it.m_data; }
#endif

			BHAVESH_CXX20_CONSTEXPR element_type& operator*() const {
				return *m_data;
			}
			BHAVESH_CXX20_CONSTEXPR element_type* operator->() const {
				return m_data;
			}

			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator& operator++() { m_data += n; return *this; }
			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator  operator++(int) { matrix_column_iterator cpy = *this; ++*this; return cpy; }
			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator& operator--() { m_data -= n; return *this; }
			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator  operator--(int) { matrix_column_iterator cpy = *this; --*this; return cpy;   }

			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator& operator+=(ptrdiff_t n) { m_data += n * this->n; return *this; }
			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator& operator-=(ptrdiff_t n) { m_data -= n * this->n; return *this; }

			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator  operator+ (ptrdiff_t n) const { matrix_column_iterator cpy = *this; cpy += n; return cpy; }
			BHAVESH_CXX20_CONSTEXPR matrix_column_iterator  operator- (ptrdiff_t n) const { matrix_column_iterator cpy = *this; cpy -= n; return cpy; }

			BHAVESH_CXX20_CONSTEXPR ptrdiff_t      operator- (const matrix_column_iterator& it) const { return (m_data - it.m_data) / static_cast<ptrdiff_t>(n); }

			BHAVESH_CXX20_CONSTEXPR element_type&  operator[](ptrdiff_t n) const {
				return m_data[n * this->n];
			}
		private:
			size_t n = 0;
			T* m_data = nullptr;
		};

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR matrix_column_iterator<T> operator+(ptrdiff_t n, const matrix_column_iterator<T>& it) {
			return it + n;
		}


	} }

	template <typename T>
	class matrix_row BHAVESH_CXX20_MATRIX_VIEW {
	public:
		using iterator = matrix_iterators::matrix_row_iterator<T>;
		using const_iterator = matrix_iterators::matrix_row_iterator<const T>;
		using value_type = std::iterator_traits<iterator>::value_type;
		using element_type = std::remove_cv_t<value_type>;

		explicit BHAVESH_CXX20_CONSTEXPR matrix_row(size_t /* m */, size_t n, T* p) : n(n), m_data(p) {}
		
		BHAVESH_CXX20_CONSTEXPR T& operator[](size_t i) {
#			ifdef BHAVESH_DEBUG
				if (i >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return m_data[i];
		}
		BHAVESH_CXX20_CONSTEXPR const T& operator[](size_t i) const {
#			ifdef BHAVESH_DEBUG
				if (i >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return m_data[i];
		}
		BHAVESH_CXX20_CONSTEXPR T& get(size_t i) {
			if (i >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return m_data[i];
		}
		BHAVESH_CXX20_CONSTEXPR const T& get(size_t i) const {
			if (i >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return m_data[i];
		}
		BHAVESH_CXX20_CONSTEXPR       T* data()       { return m_data; }
		BHAVESH_CXX20_CONSTEXPR const T* data() const { return m_data; }

		      iterator  begin()       { return       iterator{ m_data     }; }
		const_iterator  begin() const { return const_iterator{ m_data     }; }
		const_iterator cbegin() const { return const_iterator{ m_data     }; }
		      iterator    end()       { return       iterator{ m_data + n }; }
		const_iterator    end() const { return const_iterator{ m_data + n }; }
		const_iterator   cend() const { return const_iterator{ m_data + n }; }
		size_t           size() const { return n; }

	private:
		size_t n;
		T* m_data;
	};

	template <typename T>
	class matrix_column BHAVESH_CXX20_MATRIX_VIEW {
	public:
		using iterator = matrix_iterators::matrix_column_iterator<T>;
		using const_iterator = matrix_iterators::matrix_column_iterator<const T>;
		using value_type = std::iterator_traits<iterator>::value_type;
		using element_type = std::remove_cv_t<value_type>;
		explicit BHAVESH_CXX20_CONSTEXPR matrix_column(size_t m, size_t n, T* p) : m(m), n(n), m_data(p) {}

		BHAVESH_CXX20_CONSTEXPR T& operator[](size_t i) {
#			ifdef BHAVESH_DEBUG
				if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return m_data[i*n];
		}
		BHAVESH_CXX20_CONSTEXPR const T& operator[](size_t i) const {
#			ifdef BHAVESH_DEBUG
				if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return m_data[i*n];
		}
		BHAVESH_CXX20_CONSTEXPR T& get(size_t i) {
			if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return m_data[i*n];
		}
		BHAVESH_CXX20_CONSTEXPR const T& get(size_t i) const {
			if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return m_data[i*n];
		}

		iterator        begin()       { return       iterator{ n, m_data       }; }
		const_iterator  begin() const { return const_iterator{ n, m_data       }; }
		const_iterator cbegin() const { return const_iterator{ n, m_data       }; }
		iterator          end()       { return       iterator{ n, m_data + m*n }; }
		const_iterator    end() const { return const_iterator{ n, m_data + m*n }; }
		const_iterator   cend() const { return const_iterator{ n, m_data + m*n }; }
		size_t           size() const { return m; }

	private:
		size_t m;
		size_t n;
		T* m_data;
	};

}



#endif // !BHAVESH_MATRIX_H

