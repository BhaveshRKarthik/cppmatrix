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
#   define BHAVESH_CXX17 true
# else
#   define BHAVESH_CXX17_CONSTEXPR 
#   define BHAVESH_CXX17 false
# endif

# ifdef BHAVESH_DEBUG
#   if BHAVESH_DEBUG
#     define BHAVESH_USE_IF_DEBUG(...) __VA_ARGS__
#   else
#     define BHAVESH_USE_IF_DEBUG(...) 
#   endif
# elif !defined(BHAVESH_NO_DEDUCE_DEBUG) && (defined(_DEBUG) || !defined(NDEBUG))
#   define BHAVESH_DEBUG true
#   define BHAVESH_USE_IF_DEBUG(...) __VA_ARGS__
# else
#   define BHAVESH_DEBUG false
#  define BHAVESH_USE_IF_DEBUG(...)
# endif

#endif

#if BHAVESH_CXX_VER < 201402L
# error "bhavesh_matrix.h needs atleast c++14"
#endif

#ifndef BHAVESH_MATRIX_H
#define BHAVESH_MATRIX_H 0.2

#if BHAVESH_CXX20
# define BHAVESH_CXX20_MATRIX_VIEW : public std::ranges::view_base
#else
# define BHAVESH_CXX20_MATRIX_VIEW
#endif

#include <cstddef> // size_t, ptrdiff_t
#include <cstring> // std::memset, std::memcpy
#include <utility> // std::forward, std::move, std::initializer_list...
#include <new> // placement new
#include <memory> // std::construct_at | std::destroy...
#include <stdexcept> // exception types
#include <iterator> // iterator_tag
#include <complex> // complex numbers of fields are fields
#include <type_traits> // commonly used; enable_if...
#include <algorithm> // for_each
#if BHAVESH_CXX17
#include <execution> // execution_policy
#endif
#if BHAVESH_CXX20
#include <concepts> // used with ranges
#include <ranges>   // view_base
#include <compare>  // spaceship operator
#endif

namespace bhavesh {

#ifndef BHAVESH_SILENCE_T
#define BHAVESH_SILENCE_T

	inline namespace detail {
	namespace silence_detail { /* i dont want silence_xxxx of type silence_t to be in my namespace, rather their integral_constant versions */
		enum silence_t : std::uint8_t {
			silence_none,
			silence_less,
			silence_more,
			silence_both
		};
	} 
	}

	using detail::silence_detail::silence_t;

	template<silence_t sil>
	using silence_constant = std::integral_constant<silence_t, sil>; /* cant use silence_type twice! */

	template<typename s>
	struct is_silence_type : std::false_type {};
	template<silence_t sil>
	struct is_silence_type<silence_constant<sil>> : std::true_type {};
	/* playing good with later features */
#if BHAVESH_CXX17
	template <typename s>
	constexpr bool is_silence_type_v = is_silence_type<s>::value;
#endif
#if BHAVESH_CXX20
	template <typename s>
	concept silence_type = is_silence_type_v<s>;
#endif

	constexpr const auto silence_none = silence_constant<silence_t::silence_none>{};
	constexpr const auto silence_less = silence_constant<silence_t::silence_less>{};
	constexpr const auto silence_more = silence_constant<silence_t::silence_more>{};
	constexpr const auto silence_both = silence_constant<silence_t::silence_both>{};

#endif // !BHAVESH_SILENCE_T

	inline namespace detail {
	namespace matrix_detail {

		// wrappers for easier access + more readability
		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* allocate(std::size_t s) {
			return static_cast<T*>(std::allocator<T>{}.allocate(s));
		}

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR void deallocate(T* ptr, std::size_t s) {
			std::allocator<T>{}.deallocate(ptr, s);
		}

#   if BHAVESH_CXX20
		// in C++20 and beyond, these are blessed with constexpr 
		using std::construct_at;
		using std::destroy_at;
		using std::destroy_n;
#   else
		// manual implementation as not all of these existed in C++14 | lower number of preprocessor ifs by not branching for C++17
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
		inline ForwardIt destroy_n(ForwardIt first, Size n)
		{
			for (; n > 0; (void) ++first, --n)
				destroy_at(std::addressof(*first));
			return first;
		}
#   endif
		// "helper" for more readable code
		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR void construct_default_at(T* ptr) {
			(construct_at)(ptr /* no arguments */);
		}

		// exception type; should be self-explanatory
		class incompletely_initialized : public std::runtime_error { using runtime_error::runtime_error; constexpr incompletely_initialized() = delete; };

		// responsible for memory during construction; works kinda-like a mix of unique_ptr<T[]> and std::vector<T> with more crazy stuff
		template<typename T>
		class construction_holder {
		public:
			BHAVESH_CXX20_CONSTEXPR explicit construction_holder() : start(nullptr), size(0), capacity(0) {}
			BHAVESH_CXX20_CONSTEXPR explicit construction_holder(std::size_t s) : start((allocate<T>)(s)), size(0), capacity(s) {}
			BHAVESH_CXX20_CONSTEXPR explicit construction_holder(std::size_t m, std::size_t n) : construction_holder(m * n) {}
			BHAVESH_CXX20_CONSTEXPR construction_holder(const construction_holder&) = delete;
			BHAVESH_CXX20_CONSTEXPR construction_holder(construction_holder&& oth) 
				:	start(std::exchange(oth.start, nullptr)), 
					size(std::exchange(oth.size, 0)), 
					capacity(std::exchange(oth.capacity, 0)) {};
			BHAVESH_CXX20_CONSTEXPR construction_holder& operator=(const construction_holder&) = delete;
			BHAVESH_CXX20_CONSTEXPR construction_holder& operator=(construction_holder&& oth) {
				if (this == &oth) return *this;
				start = std::exchange(oth.start, nullptr);
				size = std::exchange(oth.size, 0);
				capacity = std::exchange(oth.capacity, 0);
				return *this;
			}

			BHAVESH_CXX20_CONSTEXPR ~construction_holder() {
				if (start) {
					destroy_n(start, size);
					deallocate(start, capacity);
					start = nullptr;
					size = capacity = 0;
				}
			}

			template<typename ...tArgs>
			BHAVESH_CXX20_CONSTEXPR void emplace_back(tArgs&&... args) {
				(construct_at)(start + size, std::forward<tArgs>(args)...);
				size++;
			}

			BHAVESH_CXX20_CONSTEXPR void emplace_default() {
				(construct_default_at)(start + size);
				size++;
			}

			template <typename X = T, std::enable_if_t<!std::is_trivially_default_constructible<X>::value, int> = 0>
			BHAVESH_CXX20_CONSTEXPR void fill_default() {
				while (size != capacity) emplace_default();
			}
			template <typename X = T, std::enable_if_t<std::is_trivially_default_constructible<X>::value, long> = 0>
			BHAVESH_CXX20_CONSTEXPR void fill_default() {
#if BHAVESH_CXX20
				if (std::is_constant_evaluated()) {
					while (size != capacity) emplace_default();
					return;
				}
#endif
				std::memset(static_cast<void*>(start + size), 0, (capacity - size) * sizeof(T));
				capacity = size;
			}

			template <typename X = T, std::enable_if_t<!std::is_trivially_default_constructible<X>::value, int> = 0>
			BHAVESH_CXX20_CONSTEXPR void insert_default_n(std::size_t s) {
				for (std::size_t i = 0; i != s; ++i) emplace_default();
			}
			template <typename X = T, std::enable_if_t<std::is_trivially_default_constructible<X>::value, long> = 0>
			BHAVESH_CXX20_CONSTEXPR void insert_default_n(std::size_t s) {
#if BHAVESH_CXX20
				if (std::is_constant_evaluated()) {
					for (std::size_t i = 0; i != s; ++i) emplace_default();
					return;
				}
#endif
				std::memset(static_cast<void*>(start + size), 0, s * sizeof(T));
				size += s;
			}


			template <typename X = T, std::enable_if_t<!std::is_trivially_copy_constructible<X>::value, int> = 0>
			BHAVESH_CXX20_CONSTEXPR void copy_from(const T* ptr, std::size_t s) {
				for (std::size_t i = 0; i != s && size != capacity; ++i) {
					emplace_back(ptr[i]);
				}
			}
			template <typename X = T, std::enable_if_t<std::is_trivially_copy_constructible<X>::value, long> = 0>
			BHAVESH_CXX20_CONSTEXPR void copy_from(const T* ptr, std::size_t s) {
#if BHAVESH_CXX20
				if (std::is_constant_evaluated()) {
					for (std::size_t i = 0; i != s && size != capacity; ++i) {
						emplace_back(ptr[i]);
					}
					return;
				}
#endif
				std::memcpy(static_cast<void*>(start + size), static_cast<const void*>(ptr), std::min(capacity - size, s) * sizeof(T));
			}


			template<bool suppress_check=!BHAVESH_DEBUG>
			BHAVESH_CXX20_CONSTEXPR T* release() {
				size = capacity = 0;
				return std::exchange(start, nullptr);
			}
			template <>
			BHAVESH_CXX20_CONSTEXPR T* release<false>() {
				if (size != capacity) throw incompletely_initialized("Not all values were fully initialized; unsafe to keep uninitialized memory around.");
				size = capacity = 0;
				return std::exchange(start, nullptr);
			}

		private:
			T* start;
			std::size_t size;
			std::size_t capacity;
		};

		// I allow users to give me the transpose of a matrix and i will transpose it again for them; this is what will be used during construction (like construction_holder)
		template<typename T>
		class construction_holder_transpose {
		public:
			explicit BHAVESH_CXX20_CONSTEXPR construction_holder_transpose() : start(nullptr), m(0), n(0) {}
			explicit BHAVESH_CXX20_CONSTEXPR construction_holder_transpose(std::size_t m, std::size_t n) : start((allocate<T>)(m*n)), m(m), n(n) {}
			BHAVESH_CXX20_CONSTEXPR construction_holder_transpose(const construction_holder_transpose&) = delete;
			BHAVESH_CXX20_CONSTEXPR construction_holder_transpose(construction_holder_transpose&& oth)
				:	start(std::exchange(oth.start, nullptr)),
					i(std::exchange(oth.i, 0)), j(std::exchange(oth.j, 0)),
					m(std::exchange(oth.m, 0)), n(std::exchange(oth.n, 0)) {}
			BHAVESH_CXX20_CONSTEXPR construction_holder_transpose& operator=(const construction_holder_transpose&) = delete;
			BHAVESH_CXX20_CONSTEXPR construction_holder_transpose& operator=(construction_holder_transpose&& oth) {
				if (this == &oth) return *this;
				start = std::exchange(oth.start, nullptr);
				i = std::exchange(oth.i, 0);
				j = std::exchange(oth.j, 0);
				m = std::exchange(oth.m, 0);
				n = std::exchange(oth.n, 0);
				return *this;
			}

			BHAVESH_CXX20_CONSTEXPR ~construction_holder_transpose() {
				if (start) {
					for (std::size_t x = 0; x != i; ++x) {
						destroy_n(start + x * n, j + 1);
					}
					for (std::size_t x = i; x != m; ++x) {
						destroy_n(start + x * n, j);
					}
					deallocate(start, m*n);
					start = nullptr;
					i = j = m = n = 0;
				}
			}

			template<typename ...tArgs>
			BHAVESH_CXX20_CONSTEXPR void emplace_back(tArgs&&... args) {
				(construct_at)(start + i * n + j, std::forward<tArgs>(args)...);
				if (++i == m) { i = 0; j++; }
			}

			BHAVESH_CXX20_CONSTEXPR void emplace_default() {
				construct_default_at(start + i * n + j);
				if (++i == m) { i = 0; j++; }
			}

		private:
			BHAVESH_CXX20_CONSTEXPR void _fill_default() {
				for (std::size_t x = 0; x != i; ++x) {
					for (std::size_t y = j + 1; y != n; ++y) {
						construct_default_at(start + x * n + y);
					}
				}
				for (std::size_t x = i; x != m; ++x) {
					for (std::size_t y = j; y != n; ++y) {
						construct_default_at(start + x * n + y);
					}
				}
				i = 0; j = n;
			}

		public:
			template <typename X = T, std::enable_if_t<!std::is_trivially_default_constructible<X>::value, int> = 0>
			BHAVESH_CXX20_CONSTEXPR void fill_default() {
				_fill_default();
			}
			template <typename X = T, std::enable_if_t<std::is_trivially_default_constructible<X>::value, long> = 0>
			BHAVESH_CXX20_CONSTEXPR void fill_default() {
#if BHAVESH_CXX20
				if (std::is_constant_evaluated()) {
					_fill_default();
					return;
				}
#endif
				for (std::size_t x = 0; x != i; ++x) {
					std::memset(start + x * n + j + 1, 0, (n - j - 1) * sizeof(T));
				}
				if (j) {
					for (std::size_t x = i; x != m; ++x) {
						std::memset(start + x * n + j, 0, (n - j) * sizeof(T));
					}
				}
				else {
					std::memset(start + i * n, 0, n * (m - i) * sizeof(T));
				}
				i = 0; j = n;
			}

			BHAVESH_CXX20_CONSTEXPR void copy_from(const T* ptr, std::size_t s) {
				for (std::size_t x = 0; x != s && j != n; ++x) {
					emplace_back(ptr[x]);
				}
			}

			BHAVESH_CXX20_CONSTEXPR void insert_default_n(std::size_t s) {
				for (std::size_t t = 0; t != s; ++t) emplace_default();
			}

			template<bool suppress_check = !BHAVESH_DEBUG>
			BHAVESH_CXX20_CONSTEXPR T* release() {
				i = j = m = n = 0;
				return std::exchange(start, nullptr);
			}
			template <>
			BHAVESH_CXX20_CONSTEXPR T* release<false>() {
				if (i != 0 || j != n) throw incompletely_initialized("Not all values were fully initialized; unsafe to keep uninitialized memory around.");
				i = j = m = n = 0;
				return std::exchange(start, nullptr);
			}

		private:
			T* start;
			std::size_t i=0, j=0;
			std::size_t m, n;
		};

		template<typename T, bool is_transpose>
		using holder = std::conditional_t<is_transpose, construction_holder_transpose<T>, construction_holder<T>>;

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_default_n(std::size_t s) {
			construction_holder<T> h(s);
			h.fill_default();
			return h.release<true>();
		}

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_fill_n(std::size_t s, const T& val) {
			construction_holder<T> h(s);
			for (std::size_t i = 0; i != s; ++i) {
				h.emplace_back(val);
			}
			return h.release<true>();
		}



		template <silence_t sil, bool is_transpose, typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_from_il(std::size_t m, std::size_t n, std::initializer_list<T> il) {
			holder<T, is_transpose> h(m, n);
			if BHAVESH_CXX17_CONSTEXPR((sil & silence_t::silence_less) == 0) if (il.size() < m * n) throw std::invalid_argument( "too few arguments given to matrix(m, n, { ... })");
			if BHAVESH_CXX17_CONSTEXPR((sil & silence_t::silence_more) == 0) if (il.size() > m * n) throw std::invalid_argument("too many arguments given to matrix(m, n, { ... })");
			h.copy_from(il.begin(), il.size());
			h.fill_default();
			return h.release<true>();
		}

		template <bool is_transpose, typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_from_matrix(std::size_t m, std::size_t n, T* mat) {
			holder<T, is_transpose> h(m, n);
			h.copy_from(mat, m*n);
			return h.release<true>();
		}

		template <silence_t sil, bool is_transpose, typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_from_ilil(std::size_t m, std::size_t n, std::initializer_list<std::initializer_list<T>> il) {
			std::conditional_t<is_transpose, construction_holder_transpose<T>, construction_holder<T>> h(m, n);
			if BHAVESH_CXX17_CONSTEXPR(is_transpose) std::swap(m, n);
			if BHAVESH_CXX17_CONSTEXPR((sil & silence_t::silence_less) == 0) if (il.size() < m) throw std::invalid_argument( "too few arguments given to matrix(m, n, { ... })");
			if BHAVESH_CXX17_CONSTEXPR((sil & silence_t::silence_more) == 0) if (il.size() > m) throw std::invalid_argument("too many arguments given to matrix(m, n, { ... })");
			for (std::size_t i = 0; i != m && i != il.size(); ++i) {
				auto& il2 = il.begin()[i];

				if BHAVESH_CXX17_CONSTEXPR((sil & silence_t::silence_less) == 0) if (il2.size() < n) throw std::invalid_argument( "too few arguments given to matrix(m, n, { ... })");
				if BHAVESH_CXX17_CONSTEXPR((sil & silence_t::silence_more) == 0) if (il2.size() > n) throw std::invalid_argument("too many arguments given to matrix(m, n, { ... })");

				h.copy_from(il2.begin(), std::min(il.size(), n));
				if (il2.size() < n) h.insert_default_n(n - il.size());
			}
			/* if (il.size() < m) */
			h.fill_default();
			return h.release<true>();
		}


#		if BHAVESH_CXX20
		/*  ranges based constructors  */
		
		template <typename R, typename T>
		concept compatible_linear_range = std::ranges::input_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;
		template <typename R, typename T>
		concept compatible_tabular_range = std::ranges::input_range<R> && compatible_linear_range<std::ranges::range_reference_t<R>, T>;
		template <typename R, typename T>
		concept matrix_compatible_range = compatible_linear_range<R, T> || compatible_tabular_range<R, T>;

		template<silence_t sil, bool fill_all, typename T, template <typename> typename holder, compatible_linear_range<T> R>
		constexpr inline void take_n_from(holder<T>& h, std::size_t n, R&& rng) {
			if constexpr (std::ranges::sized_range<R>) {
				const std::size_t x = std::ranges::size(rng);
				if constexpr ((sil & silence_t::silence_less) == 0) if (x < n) throw std::invalid_argument( "too few arguments given to matrix(from_range, m, n, { ... })");
				if constexpr ((sil & silence_t::silence_more) == 0) if (x > n) throw std::invalid_argument("too many arguments given to matrix(from_range, m, n, { ... })");
				std::size_t i = 0;
				for (auto&& v : rng) {
					if (i == n) break;
					h.emplace_back(std::forward<std::ranges::range_reference_t<R>>(v));
					i++;
				}
				if constexpr (fill_all) h.fill_default();
				else                    h.insert_default_n(n - i);
			}
			else {
				std::size_t i = 0;
				for (auto&& v : rng) {
					if (i == n) {
						if constexpr ((sil & silence_t::silence_more) == 0) {
							throw std::invalid_argument("too many arguments given to matrix(from_range, m, n, { ... })");
						}
						break;
					}
					h.emplace_back(std::forward<std::ranges::range_reference_t<R>>(v));
					i++;
				}

				if constexpr ((sil & silence_t::silence_less) == 0) if (i < n) throw std::invalid_argument("too few arguments given to matrix(from_range, m, n, { ... })");

				if constexpr (fill_all) h.fill_default();
				else                    h.insert_default_n(n - i);
			}
		}

		template<silence_t sil, bool is_transpose, typename T, compatible_linear_range<T> R>
		constexpr inline T* create_from_range(std::size_t m, std::size_t n, R&& rng) {
			std::conditional_t<is_transpose, construction_holder_transpose<T>, construction_holder<T>> h(m, n);
			take_n_from<sil, true>(h, m * n, std::forward<R>(rng));
			return h.release<true>();
		}

		template<silence_t sil, bool is_transpose, typename T, compatible_tabular_range<T> R>
		constexpr inline T* create_from_range(std::size_t m, std::size_t n, R&& rng) {
			std::conditional_t<is_transpose, construction_holder_transpose<T>, construction_holder<T>> h(m, n);
			if constexpr (is_transpose) std::swap(m, n);

			if constexpr (std::ranges::sized_range<R>) {
				const std::size_t x = std::ranges::size(rng);
				if constexpr ((sil & silence_t::silence_less) == 0) if (x < m) throw std::invalid_argument( "too few arguments given to matrix(from_range, m, n, { ... })");
				if constexpr ((sil & silence_t::silence_more) == 0) if (x > m) throw std::invalid_argument("too many arguments given to matrix(from_range, m, n, { ... })");
				std::size_t i = 0;
				for (auto&& v : rng) {
					if (i == m) break;
					take_n_from<sil, false>(h, n, std::forward<std::ranges::range_reference_t<R>>(v));
				}
				h.fill_default();
				return h.release<true>();
			}
			else {
				std::size_t i = 0;
				for (auto&& v : rng) {
					if (i == m) {
						if constexpr ((sil & silence_t::silence_more) == 0) {
							throw std::invalid_argument("too many arguments given to matrix(from_range, m, n, { ... })");
						}
						break;
					}
					take_n_from<sil, false>(h, n, std::forward<std::ranges::range_reference_t<R>>(v));
				}
				if constexpr ((sil & silence_t::silence_less) == 0) if (i < m) throw std::invalid_argument("too few arguments given to matrix(from_range, m, n, { ... })");
				h.fill_default();
				return h.release<true>();
					
			}
		}

#		endif
	}
	}

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
			constexpr _row_iterator() = default;
			constexpr _row_iterator(T* data) : m_data(data) {}
			constexpr _row_iterator(const _row_iterator&) = default;
			constexpr _row_iterator(_row_iterator&&) = default;
			constexpr _row_iterator& operator=(const _row_iterator&) = default;
			constexpr _row_iterator& operator=(_row_iterator&&) = default;

			template<typename=std::enable_if_t<!std::is_const<T>::value>>
			constexpr operator _row_iterator<const T, _tag>() {
				return _row_iterator<const T, _tag>(static_cast<const T*>(m_data));
			}

		public:
#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const _row_iterator& it) const { return m_data <=> it.m_data; }
			constexpr bool operator==(const _row_iterator&) const = default;
			constexpr bool operator!=(const _row_iterator&) const = default;
#else
			constexpr bool operator==(const _row_iterator& it) const { return m_data == it.m_data; }
			constexpr bool operator!=(const _row_iterator& it) const { return m_data != it.m_data; }
			constexpr bool operator< (const _row_iterator& it) const { return m_data <  it.m_data; }
			constexpr bool operator<=(const _row_iterator& it) const { return m_data <= it.m_data; }
			constexpr bool operator> (const _row_iterator& it) const { return m_data >  it.m_data; }
			constexpr bool operator>=(const _row_iterator& it) const { return m_data >= it.m_data; }
#endif

			constexpr element_type& operator*() const {
				return *m_data;
			}
			constexpr element_type* operator->() const {
				return m_data;
			}

			constexpr _row_iterator& operator++() { m_data++;    return *this; }
			constexpr _row_iterator  operator++(int) { _row_iterator cpy = *this;     m_data++;    return cpy; }
			constexpr _row_iterator& operator--() { m_data--;    return *this; }
			constexpr _row_iterator  operator--(int) {               _row_iterator cpy = *this;     m_data--;    return cpy;   }

			constexpr _row_iterator& operator+=(ptrdiff_t n) { m_data += n; return *this; }
			constexpr _row_iterator& operator-=(ptrdiff_t n) {                                      m_data -= n; return *this; }

			constexpr _row_iterator  operator+ (ptrdiff_t n) const { _row_iterator cpy = *this; cpy.m_data += n; return cpy; }
			constexpr _row_iterator  operator- (ptrdiff_t n) const { _row_iterator cpy = *this; cpy.m_data -= n; return cpy; }

			constexpr ptrdiff_t      operator- (const _row_iterator& it) const { return m_data - it.m_data; }

			constexpr element_type&  operator[](ptrdiff_t n) const {
				return m_data[n];
			}
		private:
			T* m_data = nullptr;
		};

		template <typename T, typename _tag>
		constexpr _row_iterator<T, _tag> operator+(ptrdiff_t n, const _row_iterator<T, _tag>& it) {
			return it + n;
		}

		struct _row_iterator_tag {};
		struct _rowmajor_iterator_tag {};
		
		template <typename T>
		using matrix_row_iterator = _row_iterator<T, _row_iterator_tag>;
		template <typename T>
		using matrix_rowmajor_iterator = _row_iterator<T, _rowmajor_iterator_tag>;

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
			constexpr matrix_column_iterator() = default;
			constexpr matrix_column_iterator(size_t n, T* data) : n(n), m_data(data) {}
			constexpr matrix_column_iterator(const matrix_column_iterator&) = default;
			constexpr matrix_column_iterator(matrix_column_iterator&&) = default;
			constexpr matrix_column_iterator& operator=(const matrix_column_iterator&) = default;
			constexpr matrix_column_iterator& operator=(matrix_column_iterator&&) = default;

			template<typename=std::enable_if_t<!std::is_const<T>::value>>
			constexpr operator matrix_column_iterator<const T>() {
				return matrix_column_iterator<const T>(static_cast<const T*>(m_data));
			}

		public:
#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const matrix_column_iterator& it) const { return m_data <=> it.m_data; }
			constexpr bool operator==(const matrix_column_iterator&) const = default;
			constexpr bool operator!=(const matrix_column_iterator&) const = default;
#else
			constexpr bool operator==(const matrix_column_iterator& it) const { return m_data == it.m_data; }
			constexpr bool operator!=(const matrix_column_iterator& it) const { return m_data != it.m_data; }
			constexpr bool operator< (const matrix_column_iterator& it) const { return m_data <  it.m_data; }
			constexpr bool operator<=(const matrix_column_iterator& it) const { return m_data <= it.m_data; }
			constexpr bool operator> (const matrix_column_iterator& it) const { return m_data >  it.m_data; }
			constexpr bool operator>=(const matrix_column_iterator& it) const { return m_data >= it.m_data; }
#endif

			constexpr element_type& operator*() const {
				return *m_data;
			}
			constexpr element_type* operator->() const {
				return m_data;
			}

			constexpr matrix_column_iterator& operator++() { m_data += n; return *this; }
			constexpr matrix_column_iterator  operator++(int) { matrix_column_iterator cpy = *this; ++*this; return cpy; }
			constexpr matrix_column_iterator& operator--() { m_data -= n; return *this; }
			constexpr matrix_column_iterator  operator--(int) { matrix_column_iterator cpy = *this; --*this; return cpy;   }

			constexpr matrix_column_iterator& operator+=(ptrdiff_t n) { m_data += n * this->n; return *this; }
			constexpr matrix_column_iterator& operator-=(ptrdiff_t n) { m_data -= n * this->n; return *this; }

			constexpr matrix_column_iterator  operator+ (ptrdiff_t n) const { matrix_column_iterator cpy = *this; cpy += n; return cpy; }
			constexpr matrix_column_iterator  operator- (ptrdiff_t n) const { matrix_column_iterator cpy = *this; cpy -= n; return cpy; }

			constexpr ptrdiff_t      operator- (const matrix_column_iterator& it) const { return (m_data - it.m_data) / static_cast<ptrdiff_t>(n); }

			constexpr element_type&  operator[](ptrdiff_t n) const {
				return m_data[n * this->n];
			}
		private:
			size_t n = 0;
			T* m_data = nullptr;
		};

		template <typename T>
		constexpr matrix_column_iterator<T> operator+(ptrdiff_t n, const matrix_column_iterator<T>& it) {
			return it + n;
		}

		template <typename T>
		class matrix_colmajor_iterator {
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
			constexpr matrix_colmajor_iterator() = default;
			constexpr matrix_colmajor_iterator(std::size_t m, std::size_t n, T* data) : m(m), n(n), m_data(data) {}
			constexpr matrix_colmajor_iterator(const matrix_colmajor_iterator&) = default;
			constexpr matrix_colmajor_iterator(matrix_colmajor_iterator&&) = default;
			constexpr matrix_colmajor_iterator& operator=(const matrix_colmajor_iterator&) = default;
			constexpr matrix_colmajor_iterator& operator=(matrix_colmajor_iterator&&) = default;
#if BHAVESH_CXX20
			constexpr std::strong_ordering operator<=>(const matrix_colmajor_iterator& oth) const {
				return (m_data == oth.m_data) ? (i <=> oth.i) : (m_data <=> oth.m_data);
			}
			constexpr bool operator==(const matrix_colmajor_iterator&) const = default;
			constexpr bool operator!=(const matrix_colmajor_iterator&) const = default;
#else
			constexpr bool operator==(const matrix_colmajor_iterator& oth) const { return m_data == oth.m_data && i == oth.i; }
			constexpr bool operator!=(const matrix_colmajor_iterator& oth) const { return m_data != oth.m_data || i != oth.i; }
			constexpr bool operator< (const matrix_colmajor_iterator& oth) const { return m_data <= oth.m_data || i <  oth.i; }
			constexpr bool operator<=(const matrix_colmajor_iterator& oth) const { return m_data <= oth.m_data && i <= oth.i; }
			constexpr bool operator> (const matrix_colmajor_iterator& oth) const { return m_data >= oth.m_data && i >  oth.i; }
			constexpr bool operator>=(const matrix_colmajor_iterator& oth) const { return m_data >= oth.m_data && i >= oth.i; }
#endif

			constexpr element_type& operator*() const {
				return m_data[i*n];
			}
			constexpr element_type* operator->() const {
				return m_data + i*n;
			}

			constexpr matrix_colmajor_iterator& operator++()                { i++; if (i == m) { i = 0; m_data++; } return *this; }
			constexpr matrix_colmajor_iterator  operator++(int) { auto cpy = *this; ++*this; return cpy; }
			constexpr matrix_colmajor_iterator& operator--()              { i--; if (i == 0) { i = m-1; m_data--; } return *this; }
			constexpr matrix_colmajor_iterator  operator--(int) { auto cpy = *this; --*this; return cpy; }

			constexpr matrix_colmajor_iterator& operator+=(ptrdiff_t n) { i += n; m_data += i / m; i %= m; if (i < 0) { i += m; m_data--; } return *this; }
			constexpr matrix_colmajor_iterator& operator-=(ptrdiff_t n) { return (*this += (-n)); }

			constexpr matrix_colmajor_iterator  operator+ (ptrdiff_t n) const { auto cpy = *this; cpy += n; return cpy; }
			constexpr matrix_colmajor_iterator  operator- (ptrdiff_t n) const { auto cpy = *this; cpy -= n; return cpy; }

			constexpr ptrdiff_t      operator- (const matrix_colmajor_iterator& it) const { return (m_data - it.m_data) * m + i - it.i; }

			constexpr element_type& operator[](ptrdiff_t n) const {
				return *(*this + n);
			}
		private:
			std::size_t i = 0;
			std::size_t m = 0, n = 0;
			T* m_data = nullptr;
		};

		template <typename T>
		constexpr matrix_colmajor_iterator<T> operator+(ptrdiff_t n, const matrix_colmajor_iterator<T>& it) {
			return it + n;
		}

		class iota_iterator {
		public:
			using value_type = std::size_t;
			using pointer = void;
			using reference = const value_type&;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::random_access_iterator_tag;
#if BHAVESH_CXX20
			using iterator_concept = std::random_access_iterator_tag;
#endif

			value_type value;
			
			constexpr reference operator*() const {
				return value;
			}

			constexpr iota_iterator& operator++() { value++; return *this; }
			constexpr iota_iterator& operator--() { value--; return *this; }
			constexpr iota_iterator  operator++(int) { return iota_iterator{ value++ }; }
			constexpr iota_iterator  operator--(int) { return iota_iterator{ value-- }; }

#if BHAVESH_CXX20
			constexpr std::strong_ordering operator<=>(const iota_iterator&) const = default;
			constexpr bool operator==(const iota_iterator&) const = default;
			constexpr bool operator!=(const iota_iterator&) const = default;
#else
			constexpr bool operator==(iota_iterator it) const { return it.value == value; }
			constexpr bool operator!=(iota_iterator it) const { return it.value != value; }
			constexpr bool operator< (iota_iterator it) const { return it.value <  value; }
			constexpr bool operator<=(iota_iterator it) const { return it.value <= value; }
			constexpr bool operator> (iota_iterator it) const { return it.value >  value; }
			constexpr bool operator>=(iota_iterator it) const { return it.value >= value; }
#endif

			friend constexpr iota_iterator operator+(difference_type n, iota_iterator it) { return iota_iterator{ it.value + n }; }
			constexpr iota_iterator   operator+ (difference_type n) const { return iota_iterator{ value + n }; }
			constexpr iota_iterator   operator- (difference_type n) const { return iota_iterator{ value - n }; }
			constexpr iota_iterator&  operator+=(difference_type n)       { value += n; return *this; }
			constexpr iota_iterator&  operator-=(difference_type n)       { value -= n; return *this; }
			constexpr difference_type operator- (iota_iterator  it) const { return static_cast<difference_type>(value - it.value); }
			constexpr reference       operator[](difference_type n) const { return value + n; }
		};
	} }

	template <typename T>
	class matrix_row BHAVESH_CXX20_MATRIX_VIEW {
	public:
		using iterator = matrix_iterators::matrix_row_iterator<T>;
		using const_iterator = matrix_iterators::matrix_row_iterator<const T>;
		using value_type = std::iterator_traits<iterator>::value_type;
		using element_type = std::remove_cv_t<value_type>;

		explicit BHAVESH_CXX20_CONSTEXPR matrix_row(size_t /* m */, size_t n, T* p) : n(n), m_data(p) {}
		
		template<typename = std::enable_if_t<!std::is_const<T>::value>>
		BHAVESH_CXX20_CONSTEXPR operator matrix_row<const T>() {
			return matrix_row<const T>(std::size_t() /* unused */, n, static_cast<const T*>(m_data));
		}

		BHAVESH_CXX20_CONSTEXPR T& operator[](size_t i) {
#			if BHAVESH_DEBUG
				if (i >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return m_data[i];
		}
		BHAVESH_CXX20_CONSTEXPR const T& operator[](size_t i) const {
#			if BHAVESH_DEBUG
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
		
		template<typename = std::enable_if_t<!std::is_const<T>::value>>
		BHAVESH_CXX20_CONSTEXPR operator matrix_column<const T>() {
			return matrix_column<const T>(m, n, static_cast<const T*>(m_data));
		}
		BHAVESH_CXX20_CONSTEXPR T& operator[](size_t i) {
#			if BHAVESH_DEBUG
				if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return m_data[i*n];
		}
		BHAVESH_CXX20_CONSTEXPR const T& operator[](size_t i) const {
#			if BHAVESH_DEBUG
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

	inline namespace detail { namespace matrix_detail { 
		class transpose_t {}; 
		class take_ownership_t {}; 

		template<typename T1, typename T2>
		using addition_t = std::decay_t<decltype(std::declval<T1>() + std::declval<T2>())>;

		template<typename T1, typename T2>
		using subtraction_t = std::decay_t<decltype(std::declval<T1>() - std::declval<T2>())>;
		
		template<typename T1, typename T2>
		using multiplication_t = std::decay_t<decltype(std::declval<T1>() * std::declval<T2>())>;

	} }
	
	constexpr auto transpose = matrix_detail::transpose_t{};
	constexpr auto matrix_take_ownership = matrix_detail::take_ownership_t{};

	template <typename T> class matrix;

	template <typename> struct is_matrix : std::false_type {};
	template <typename T> struct is_matrix<matrix<T>> : std::true_type {};
#if BHAVESH_CXX17
	template <typename T>
	constexpr bool is_matrix_v = is_matrix<T>::value;
#endif
#if BHAVESH_CXX20
	template <typename T>
	concept matrix_like = is_matrix_v<T>;
#endif

	template <typename T>
	class matrix {
	private: /* helper using declarations */
		template <typename T_>
		using holder = matrix_detail::construction_holder<T_>;
		template <typename T_>
		using transpose_holder = matrix_detail::construction_holder_transpose<T_>;
	public: /* checks to make sure you dont do dumb stuff */
		static_assert(!std::is_reference_v<T>, "matrix of reference type is ill-defined");
		static_assert(!std::is_const_v<T>, "matrix<const T> is ill-defined; use const matrix<T> instead");
		static_assert(!std::is_volatile_v<T>, "matrix<volatile T> is ill-defined; use volatile matrix<T> instead (otherwise, why are you even making a matrix of volatile integers?)");
#if BHAVESH_DEBUG
		static_assert(std::is_nothrow_destructible_v<T>, "NEVER THROW IN YOUR DESTRUCTORS!!!");
#endif

	public:
		using value_type = T;

	public: /* constructors (yes there are really 23 constructors) and destructor */
		BHAVESH_CXX20_CONSTEXPR matrix() : m_data(nullptr), m(0), n(0) {}
		BHAVESH_CXX20_CONSTEXPR matrix(matrix&& mat) noexcept : m_data(std::exchange(mat.m_data, nullptr)), m(std::exchange(mat.m, 0)), n(std::exchange(mat.n, 0)) {}
		
		BHAVESH_CXX20_CONSTEXPR matrix(matrix_detail::take_ownership_t, T*  (&data), std::size_t m, std::size_t n) noexcept : m_data(std::exchange(data, nullptr)), m(m), n(n) {}
		BHAVESH_CXX20_CONSTEXPR matrix(matrix_detail::take_ownership_t, T* (&&data), std::size_t m, std::size_t n) noexcept : m_data(std::exchange(data, nullptr)), m(m), n(n) {}

		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n) : m_data(matrix_detail::create_default_n<T>(m * n)), m(m), n(n) {}
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, const T& v) : m_data(matrix_detail::create_fill_n(m * n, v)), m(m), n(n) {}

		template<typename silence, typename = std::enable_if_t<is_silence_type<silence>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<T> il, silence) :
			m_data(matrix_detail::create_from_il<silence{}, false>(m, n, il)), m(m), n(n) {}
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<T> il) : matrix(m, n, il, silence_none) {}
		
		template<typename silence, typename = std::enable_if_t<is_silence_type<silence>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<T> il, silence, matrix_detail::transpose_t) :
			m_data(matrix_detail::create_from_il<silence{}, true>(m, n, il)), m(m), n(n) {}
		template<typename silence, typename = std::enable_if_t<is_silence_type<silence>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<T> il, matrix_detail::transpose_t, silence) :
			m_data(matrix_detail::create_from_il<silence{}, true>(m, n, il)), m(m), n(n) {}
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<T> il, matrix_detail::transpose_t) : matrix(m, n, il, transpose, silence_none) {}

		template<typename silence, typename = std::enable_if_t<is_silence_type<silence>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<std::initializer_list<T>> il, silence) :
			m_data(matrix_detail::create_from_ilil<silence{}, false>(m, n, il)), m(m), n(n) {}
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<std::initializer_list<T>> il) : matrix(m, n, il, silence_none) {}

		template<typename silence, typename = std::enable_if_t<is_silence_type<silence>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<std::initializer_list<T>> il, silence, matrix_detail::transpose_t) :
			m_data(matrix_detail::create_from_ilil<silence{}, true>(m, n, il)), m(m), n(n) {}
		template<typename silence, typename = std::enable_if_t<is_silence_type<silence>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<std::initializer_list<T>> il, matrix_detail::transpose_t, silence) :
			m_data(matrix_detail::create_from_ilil<silence{}, true>(m, n, il)), m(m), n(n) {}
		BHAVESH_CXX20_CONSTEXPR matrix(std::size_t m, std::size_t n, std::initializer_list<std::initializer_list<T>> il, matrix_detail::transpose_t) : matrix(m, n, il, transpose, silence_none) {}

		BHAVESH_CXX20_CONSTEXPR matrix(const matrix& mat) : m_data(matrix_detail::create_from_matrix<false>(mat.m, mat.n, mat.m_data)), m(mat.m), n(mat.n) {}
		BHAVESH_CXX20_CONSTEXPR matrix(const matrix& mat, matrix_detail::transpose_t) : m_data(matrix_detail::create_from_matrix<true>(mat.m, mat.n, mat.m_data)), m(mat.m), n(mat.n) {}

#if BHAVESH_CXX20 /* range based constructors */
		
		template <matrix_detail::matrix_compatible_range<T> R, silence_type silence>
		constexpr matrix(std::from_range_t, R&& rng, std::size_t m, std::size_t n, silence) : m_data(matrix_detail::create_from_range<silence{}, false, T>(m, n, std::forward<R>(rng))), m(m), n(n) {}
		template <matrix_detail::matrix_compatible_range<T> R, silence_type silence>
		constexpr matrix(std::from_range_t, R&& rng, std::size_t m, std::size_t n, silence, matrix_detail::transpose_t) : m_data(matrix_detail::create_from_range<silence{}, true, T>(m, n, std::forward<R>(rng))), m(m), n(n) {}
		template <matrix_detail::matrix_compatible_range<T> R, silence_type silence>
		constexpr matrix(std::from_range_t, R&& rng, std::size_t m, std::size_t n, matrix_detail::transpose_t, silence) : m_data(matrix_detail::create_from_range<silence{}, true, T>(m, n, std::forward<R>(rng))), m(m), n(n) {}
		
		template <matrix_detail::matrix_compatible_range<T> R>
		constexpr matrix(std::from_range_t, R&& rng, std::size_t m, std::size_t n) : matrix(std::from_range, std::forward<R>(rng), m, n, silence_none) {}
		template <matrix_detail::matrix_compatible_range<T> R>
		constexpr matrix(std::from_range_t, R&& rng, std::size_t m, std::size_t n, matrix_detail::transpose_t) : matrix(std::from_range, std::forward<R>(rng), m, n, transpose, silence_none) {}

#endif

		BHAVESH_CXX20_CONSTEXPR ~matrix() {
			if (m_data) {
				matrix_detail::destroy_n(m_data, m * n);
				matrix_detail::deallocate(m_data, m * n);
#				if BHAVESH_DEBUG
					m = n = 0;
					m_data = nullptr;
#				endif
			}
		}

	public: /* conversion operator into another matrix */
		template<typename Oth, typename=std::enable_if_t<!std::is_same<Oth, T>::value>>
		BHAVESH_CXX20_CONSTEXPR operator matrix<Oth>() const {
			return convert_to<Oth>();
		}

		template<typename Oth, typename=std::enable_if_t<!std::is_same<Oth, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix<Oth> convert_to() const {
			holder<Oth> h(m, n);
			const std::size_t s = m * n;
			for (std::size_t i = 0; i != s; ++i) {
				h.emplace_back(m_data[i]);
			}
			return matrix<Oth>(matrix_take_ownership, h.release<true>(), m, n);
		}
		template<typename Oth, typename=std::enable_if_t<!std::is_same<Oth, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix<Oth> convert_to(matrix_detail::transpose_t) const {
			transpose_holder<Oth> h(m, n);
			const std::size_t s = m * n;
			for (std::size_t i = 0; i != s; ++i) {
				h.emplace_back(m_data[i]);
			}
			return matrix<Oth>(matrix_take_ownership, h.release<true>(), n, m);
		}

	public: /* assignment operators and transpose functions */
		BHAVESH_CXX20_CONSTEXPR matrix& operator=(const matrix& oth) {
			if (&oth == this) return *this;
			const size_t s = oth.m * oth.n;
			if (oth.m * oth.n == m * n) {
				for (size_t i = 0; i != s; ++i) {
					m_data[i] = oth.m_data[i];
				}
			}
			else {
				matrix_detail::destroy_n(m_data, m * n);
				matrix_detail::deallocate(m_data, m * n);
				m_data = matrix_detail::create_from_matrix<false>(oth.m, oth.n, oth.m_data);
			}
			m = oth.m;
			n = oth.n;
			return *this;
		}
		BHAVESH_CXX20_CONSTEXPR matrix& operator=(matrix&& oth) noexcept {
			if (&oth == this) return *this;
			if (m_data) {
				matrix_detail::destroy_n(m_data, m * n);
				matrix_detail::deallocate(m_data, m * n);
			}
			m = std::exchange(oth.m, 0);
			n = std::exchange(oth.n, 0);
			m_data = std::exchange(oth.m_data, nullptr);
			return *this;
		}

		BHAVESH_CXX20_CONSTEXPR matrix make_transpose() const {
			return matrix(*this, transpose);
		}

		BHAVESH_CXX20_CONSTEXPR matrix& transpose_inplace() & {
			// WARNING: MOVES TO NEW MATRIX AND THEN MOVES IT BACK; USES sizeof(T)*m*n + O(1) MEMORY IF M != N; USE transpose() IF YOU WANT A COPY
			if (m == n)
#if BHAVESH_CXX20
				[[likely]]
#endif
			{
				for (size_t i = 0; i < n; ++i) {
					for (size_t j = i + 1; j < n; ++j) {
						std::swap(m_data[i * n + j], m_data[j * n + i]);
					}
				}
			}
			else { // expensive
				T* cpy = matrix_detail::allocate<T>(m * n);
				for (size_t i = 0; i < n; ++i) {
					for (size_t j = 0; j < m; ++j) {
						(matrix_detail::construct_at)(cpy + i * m + j, std::move_if_noexcept(m_data[j * n + i]));
					}
				}
				const size_t s = m * n;
				for (size_t i = 0; i < s; ++i) {
					m_data[i] = std::move_if_noexcept(cpy[i]);
				}
				matrix_detail::destroy_n(cpy, s);
				matrix_detail::deallocate(cpy, s);
				std::swap(m, n);
			}
			return *this;
		}

		BHAVESH_CXX20_CONSTEXPR matrix&& transpose_inplace() && {
			return std::move(transpose_inplace());
		}
	public: /* shape information */

		BHAVESH_CXX20_CONSTEXPR std::size_t size() const { return m * n; }
		BHAVESH_CXX20_CONSTEXPR std::pair<std::size_t, std::size_t> shape() const { return { m, n }; }

	public: /* comparison; note: no operator< as there is no sensible general operator< implementation */
		template<typename Oth>
		BHAVESH_CXX20_CONSTEXPR bool operator==(const matrix<Oth>& oth) const {
			if (shape() != oth.shape()) return false;
			const size_t s = m * n;
			for (size_t i = 0; i < s; ++i) {
				if (m_data[i] != oth(i)) return false;
			}
			return true;
		}

		template<typename Oth>
		BHAVESH_CXX20_CONSTEXPR bool operator!=(const matrix<Oth>& oth) const { return !((*this) == oth); }

	public: /* accessors */
		BHAVESH_CXX20_CONSTEXPR matrix_row<T> operator[](std::size_t i) {
#			if BHAVESH_DEBUG
				if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return matrix_row<T>(m, n, m_data + n * i);
		}
		BHAVESH_CXX20_CONSTEXPR matrix_row<const T> operator[](std::size_t i) const {
#			if BHAVESH_DEBUG
				if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#			endif
			return matrix_row<const T>(m, n, m_data + n * i);
		}

		BHAVESH_CXX20_CONSTEXPR T& operator()(std::size_t idx) {
#			if BHAVESH_DEBUG
				if (idx >= m * n) throw std::out_of_range("Out of range element access attempted for matrix(idx)");
#			endif
			return m_data[idx];
		}
		BHAVESH_CXX20_CONSTEXPR const T& operator()(std::size_t idx) const {
#			if BHAVESH_DEBUG
				if (idx >= m * n) throw std::out_of_range("Out of range element access attempted for matrix(idx)");
#			endif
			return m_data[idx];
		}

		BHAVESH_CXX20_CONSTEXPR T& operator()(std::size_t i, std::size_t j) {
#			if BHAVESH_DEBUG
				if (i >= m || j >= n) throw std::out_of_range("Out of range element access attempted for matrix(i, j)");
#			endif
			return m_data[i * n + j];
		}
		BHAVESH_CXX20_CONSTEXPR const T& operator()(std::size_t i, std::size_t j) const {
#			if BHAVESH_DEBUG
				if (i >= m || j >= n) throw std::out_of_range("Out of range element access attempted for matrix(i, j)");
#			endif
			return m_data[i * n + j];
		}

		BHAVESH_CXX20_CONSTEXPR T& get(size_t i, size_t j) {
			if (i >= m || j >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return m_data[i * n + j];
		}

		BHAVESH_CXX20_CONSTEXPR const T& get(size_t i, size_t j) const {
			if (i >= m || j >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return m_data[i * n + j];
		}

	public:
		BHAVESH_CXX20_CONSTEXPR T& _get(size_t i, size_t j) {
			return m_data[i * n + j];
		}
		BHAVESH_CXX20_CONSTEXPR const T& _get(size_t i, size_t j) const {
			return m_data[i * n + j];
		}
		
		BHAVESH_CXX20_CONSTEXPR T& _get(size_t idx) {
			return m_data[idx];
		}
		BHAVESH_CXX20_CONSTEXPR const T& _get(size_t idx) const {
			return m_data[idx];
		}

	public:
		BHAVESH_CXX20_CONSTEXPR T get_default(size_t i, size_t j, T default_value = T{}) const noexcept {
			if (i >= m || j >= n) return std::move_if_noexcept(default_value);
			return m_data[i * n + j];
		}

	public: /* addition */
		template<typename By, typename To=matrix_detail::addition_t<const T&, const By&>>
		BHAVESH_CXX20_CONSTEXPR matrix<To> add(const matrix<By>& oth) const& {
			if (oth.shape() != shape()) throw std::invalid_argument("Addition of matrices requires same shape");
			const std::size_t s = m * n;
			holder<To> h(s);
			for (std::size_t i = 0; i != s; ++i) {
				h.emplace_back(_get(i) + oth._get(i));
			}
			return matrix<To>(matrix_take_ownership, h.release<true>(), m, n);
		}

		template<typename By, typename=std::enable_if_t<std::is_same<matrix_detail::addition_t<const T&, By>, By>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix<By>&& add(matrix<By>&& oth) const& {
			if (oth.shape() != shape()) throw std::invalid_argument("Addition of matrices requires same shape");
			const std::size_t s = m * n;
			for (std::size_t i = 0; i != s; ++i) {
				oth._get(i) = _get(i) + std::move(oth._get(i));
			}
			return std::move(oth);
		}

		template<typename By, typename=std::enable_if_t<std::is_convertible<matrix_detail::addition_t<T, const By&>, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix& add_eq(const matrix<By>& oth) {
			if (oth.shape() != shape()) throw std::invalid_argument("Addition of matrices requires same shape");
			const std::size_t s = m * n;
			for (std::size_t i = 0; i != s; ++i) {
				m_data[i] = std::move(m_data[i]) + oth._get(i);
			}
			return *this;
		}

		template<typename By, typename=std::enable_if_t<std::is_same<matrix_detail::addition_t<T, const By&>, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix&& add(const matrix<By>& oth) && {
			return std::move(this->add_eq(oth));
		}

		template<typename X=T, typename=std::enable_if_t<std::is_same<matrix_detail::addition_t<T, const X&>, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix&& add(matrix&& oth) && {
			return std::move(this->add_eq(oth));
		}

		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator+(By&& by) const& {
			return this->add(std::forward<By>(by));
		}
		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator+(By&& by) && {
			return std::move(*this).add(std::forward<By>(by));
		}

		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator+=(By&& by) {
			return this->add_eq(std::forward<By>(by));
		}
	
	public: /* subtraction */
		template<typename By, typename To=matrix_detail::subtraction_t<const T&, const By&>>
		BHAVESH_CXX20_CONSTEXPR matrix<To> sub(const matrix<By>& oth) const& {
			if (oth.shape() != shape()) throw std::invalid_argument("Subtraction of matrices requires same shape");
			const std::size_t s = m * n;
			holder<To> h(s);
			for (std::size_t i = 0; i != s; ++i) {
				h.emplace_back(_get(i) - oth._get(i));
			}
			return matrix<To>(matrix_take_ownership, h.release<true>(), m, n);
		}

		template<typename By, typename=std::enable_if_t<std::is_same<matrix_detail::subtraction_t<const T&, By>, By>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix<By>&& sub(matrix<By>&& oth) const& {
			if (oth.shape() != shape()) throw std::invalid_argument("Subtraction of matrices requires same shape");
			const std::size_t s = m * n;
			for (std::size_t i = 0; i != s; ++i) {
				oth._get(i) = _get(i) - std::move(oth._get(i));
			}
			return std::move(oth);
		}

		template<typename By, typename=std::enable_if_t<std::is_convertible<matrix_detail::subtraction_t<T, const By&>, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix& sub_eq(const matrix<By>& oth) {
			if (oth.shape() != shape()) throw std::invalid_argument("Subtraction of matrices requires same shape");
			const std::size_t s = m * n;
			for (std::size_t i = 0; i != s; ++i) {
				m_data[i] = std::move(m_data[i]) - oth._get(i);
			}
			return *this;
		}

		template<typename By, typename=std::enable_if_t<std::is_same<matrix_detail::subtraction_t<T, const By&>, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix&& sub(const matrix<By>& oth) && {
			return std::move(this->sub_eq(oth));
		}

		template<typename X=T, typename=std::enable_if_t<std::is_same<matrix_detail::subtraction_t<T, const X&>, T>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix&& sub(matrix&& oth) && {
			return std::move(this->sub_eq(oth));
		}

		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator-(By&& by) const& {
			return this->sub(std::forward<By>(by));
		}
		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator-(By&& by) && {
			return std::move(*this).sub(std::forward<By>(by));
		}

		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator-=(By&& by) {
			return this->sub_eq(std::forward<By>(by));
		}
	
	public: /* scalar(-like) multiplication */
		template<typename By, typename To=matrix_detail::multiplication_t<const T&, const By&>, typename=std::enable_if_t<!is_matrix<std::decay_t<By>>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix<To> mul(const By& oth) const {
			const std::size_t s = m * n;
			holder<To> h(s);
			for (std::size_t i = 0; i != s; ++i) {
				h.emplace_back(_get(i) * oth);
			}
			return matrix<To>(matrix_take_ownership, h.release<true>(), m, n);
		}

		template<typename By, typename=std::enable_if_t<std::is_convertible<matrix_detail::multiplication_t<T, const By&>, T>::value>, typename=std::enable_if_t<!is_matrix<std::decay_t<By>>::value>>
		BHAVESH_CXX20_CONSTEXPR matrix& mul_eq(const By& oth) {
			const std::size_t s = m * n;
			for (std::size_t i = 0; i != s; ++i) {
				_get(i) = static_cast<T>(std::move(_get(i)) * oth);
			}
			return *this;
		}

	public: /* matrix-matrix multiplication */
		template<typename By, typename To=matrix_detail::multiplication_t<const T&, const By&>>
		BHAVESH_CXX20_CONSTEXPR matrix<To> mul(const matrix<By>& oth) const {
			if (shape().second != oth.shape().first) throw std::invalid_argument("Invalid shapes for multiplication of matrices");
			
			matrix<To> answer(m, oth.shape().second);

			const std::size_t m1 = this->m, l1 = this->n, n1 = oth.shape().second;
			for (std::size_t i = 0; i < m1; ++i) {
				for (std::size_t j = 0; j != l1; ++j) {
					for (std::size_t k = 0; k != n1; ++k) {
						answer._get(i, k) = answer._get(i, k) + this->_get(i, j) * oth._get(j, k);
					}
				}
			}
			return answer;
		}
#if BHAVESH_CXX17
		template<typename By, typename To=matrix_detail::multiplication_t<const T&, const By&>, typename ExecutionPolicy, typename=std::enable_if_t<std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>>>>
		matrix<To> mul(ExecutionPolicy&& policy, const matrix<By>& oth) const {
			if (shape().second != oth.shape().first) throw std::invalid_argument("Invalid shapes for multiplication of matrices");
			
			matrix<To> answer(m, oth.shape().second);

			const std::size_t m1 = this->m, l1 = this->n, n1 = oth.shape().second;
			std::for_each(std::forward<ExecutionPolicy>(policy), 
				matrix_iterators::iota_iterator{ 0 }, matrix_iterators::iota_iterator{ m1 }, [l1, n1, &oth, &answer, this](std::size_t i) {
				for (std::size_t j = 0; j != l1; ++j) {
					for (std::size_t k = 0; k != n1; ++k) {
						answer._get(i, k) = answer._get(i, k) + this->_get(i, j) * oth._get(j, k);
					}
				}
			});
			return answer;
		}
#endif
		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator*(By&& by) const {
			return this->mul(std::forward<By>(by));
		}

		template<typename By>
		BHAVESH_CXX20_CONSTEXPR decltype(auto) operator*=(By&& by) {
			return this->mul_eq(std::forward<By>(by));
		}

	private:
		T* m_data;
		std::size_t m;
		std::size_t n;
	};
		
}

#endif // !BHAVESH_MATRIX_H