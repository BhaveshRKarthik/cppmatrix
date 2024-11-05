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

#ifndef BHAVESH_SILENCE_T
#define BHAVESH_SILENCE_T

	inline namespace detail {
	namespace silence_detail {
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
	using silence_type = std::integral_constant<silence_t, sil>;

	constexpr const auto silence_none = silence_type<silence_t::silence_none>{};
	constexpr const auto silence_less = silence_type<silence_t::silence_less>{};
	constexpr const auto silence_more = silence_type<silence_t::silence_more>{};
	constexpr const auto silence_both = silence_type<silence_t::silence_both>{};

#endif // !BHAVESH_SILENCE_T

	inline namespace detail {
	namespace matrix_detail {

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* allocate(std::size_t s) {
			return static_cast<T*>(std::allocator<T>{}.allocate(s));
		}

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR void deallocate(T* ptr, std::size_t s) {
			std::allocator<T>{}.deallocate(ptr, s);
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

		template <typename T, std::enable_if_t<std::is_trivially_default_constructible<T>::valuew, long> = 0>
		BHAVESH_CXX20_CONSTEXPR void construct_default_at(T* ptr) {
			(construct_at)(ptr, T{});
		}
		template <typename T, std::enable_if_t<!std::is_trivially_default_constructible<T>::value, int> = 0>
		BHAVESH_CXX20_CONSTEXPR void construct_default_at(T* ptr) {
			(construct_at)(ptr);
		}

		class incompletely_initialized : public std::runtime_error { using runtime_error::runtime_error; constexpr incompletely_initialized() = delete; };

		template<typename T>
		class _construction_holder {
		public:
			BHAVESH_CXX20_CONSTEXPR explicit _construction_holder() : start(nullptr), size(0), capacity(0) {}
			BHAVESH_CXX20_CONSTEXPR explicit _construction_holder(std::size_t s) : start((allocate<T>)(s)), size(0), capacity(s) {}
			BHAVESH_CXX20_CONSTEXPR explicit _construction_holder(std::size_t m, std::size_t n) : _construction_holder(m * n) {}
			BHAVESH_CXX20_CONSTEXPR _construction_holder(const _construction_holder&) = delete;
			BHAVESH_CXX20_CONSTEXPR _construction_holder(_construction_holder&& oth) 
				:	start(std::exchange(oth.start, nullptr)), 
					size(std::exchange(oth.size, 0)), 
					capacity(std::exchange(oth.capacity, 0)) {};

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

			BHAVESH_CXX20_CONSTEXPR void emplace_default() {
				(construct_default_at)(start + size++);
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
				if (std::is_constant_evaluated) {
					for (std::size_t i = 0; i != s; ++i) emplace_default();
					return;
				}
#endif
				std::memset(static_cast<void*>(start + size), 0, s * sizeof(T));
				size += s;
			}


			template <typename X = T, std::enable_if_t<!std::is_trivially_default_constructible<X>::value, int> = 0>
			BHAVESH_CXX20_CONSTEXPR void copy_from(const T* ptr, std::size_t s) {
				for (std::size_t i = 0; i != s && size != capacity; ++i) {
					emplace_back(ptr[i]);
				}
			}
			template <typename X = T, std::enable_if_t<std::is_trivially_default_constructible<X>::value, long> = 0>
			BHAVESH_CXX20_CONSTEXPR void copy_from(const T* ptr, std::size_t s) {
#if BHAVESH_CXX20
				if (std::is_constant_evaluated()) {
					for (std::size_t i = 0; i != s && size != capacity; ++i) {
						emplace_back(ptr[i]);
					}
					return;
				}
#endif
				std::memcpy(static_cast<void*>(start + size), static_cast<const void*>(ptr), std::min(capacity - size, il.size()) * sizeof(T));
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

		template<typename T>
		class _construction_holder_transpose {
		public:
			explicit BHAVESH_CXX20_CONSTEXPR _construction_holder_transpose() : start(nullptr), m(0), n(0) {}
			explicit BHAVESH_CXX20_CONSTEXPR _construction_holder_transpose(std::size_t m, std::size_t n) : start((allocate<T>)(m*n)), m(m), n(n) {}
			BHAVESH_CXX20_CONSTEXPR _construction_holder_transpose(const _construction_holder_transpose&) = delete;
			BHAVESH_CXX20_CONSTEXPR _construction_holder_transpose(_construction_holder_transpose&& oth)
				:	start(std::exchange(oth.start, nullptr)),
					i(std::exchange(oth.i, 0)), j(std::exchange(oth.j, 0)),
					m(std::exchange(oth.m, 0)), n(std::exchange(oth.n, 0)) {}

			BHAVESH_CXX20_CONSTEXPR ~_construction_holder_transpose() {
				if (start) {
					for (std::size_t x = 0; x != i; ++i) {
						destroy_n(start + i * n, j + 1);
					}
					for (std::size_t x = i; x != m; ++i) {
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

			BHAVESH_CXX20_CONSTEXPR void emplace_default() {
				construct_default_at(start + i++ * n + j);
				if (i == m) { i = 0; j++; }
			}

		private:
			BHAVESH_CXX20_CONSTEXPR void _fill_default() {
				for (std::size_t x = 0; x != i; ++i) {
					for (std::size_t y = j + 1; y != n; ++j) {
						construct_default_at(start + x * n + y);
					}
				}
				for (std::size_t x = i; x != m; ++i) {
					for (std::size_t y = j; y != n; ++j) {
						construct_default_at(start + x * n + y);
					}
				}
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
				for (std::size_t x = 0; x != i; ++i) {
					std::memset(start + x * n + j + 1, 0, (n - j - 1) * sizeof(T));
				}
				if (j) {
					for (std::size_t x = i; x != m; ++i) {
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
				for (std::size_t i = 0; i != s; ++i) emplace_default();
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


		template <typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_default_n(std::size_t s) {
			_construction_holder<T> h(s);
			h.fill_default();
			return h.release<true>();
		}

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_fill_n(std::size_t s, const T& val) {
			_construction_holder<T> h(s);
			for (std::size_t i = 0; i != s; ++i) {
				h.emplace_back(val);
			}
			return h.release<true>();
		}



		template <silence_t sil, template<typename>typename holder, typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_from_il(std::size_t m, std::size_t n, std::initializer_list<T> il) {
			holder<T> h(m, n);
			if BHAVESH_CXX17_CONSTEXPR (sil & silence_t::silence_less == 0) if (il.size() < m * n) throw std::invalid_argument( "too few arguments given to matrix(m, n, { ... })");
			if BHAVESH_CXX17_CONSTEXPR (sil & silence_t::silence_more == 0) if (il.size() > m * n) throw std::invalid_argument("too many arguments given to matrix(m, n, { ... })");
			h.copy_from(il.begin(), il.size());
			h.fill_default();
			return h.release<true>();
		}

		template <template<typename>typename holder, typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_from_matrix(std::size_t m, std::size_t n, T* mat) {
			holder<T> h(m, n);
			h.copy_from(mat, m*n);
			return h.release<true>();
		}

		template <silence_t sil, bool is_transpose, typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_from_ilil(std::size_t m, std::size_t n, std::initializer_list<std::initializer_list<T>> il) {
			std::conditional_t<is_transpose, _construction_holder_transpose<T>, _construction_holder<T>> h(m, n);
			if BHAVESH_CXX17_CONSTEXPR(is_transpose) std::swap(m, n);
			if BHAVESH_CXX17_CONSTEXPR(sil & silence_t::silence_less == 0) if (il.size() < n) throw std::invalid_argument("too few arguments given to matrix(m, n, { ... })");
			if BHAVESH_CXX17_CONSTEXPR(sil & silence_t::silence_more == 0) if (il.size() > n) throw std::invalid_argument("too many arguments given to matrix(m, n, { ... })");
			for (std::size_t i = 0; i != m && i != il.size(); ++i) {
				auto& il2 = il.begin()[i];
				h.copy_from(il2.begin(), std::min(il.size(), n));
				h.insert_default_n(std::max(n-il.size(), std::size_t(0)));
			}
			h.fill_default();
			return h.release<true>();
		}

#		if BHAVESH_CXX20
		/*  ranges based constructors  */
		
		template<silence_t sil, bool fill_all, typename T, template <typename> typename holder, std::ranges::input_range R>
		constexpr inline void take_n_from(holder<T>& h, std::size_t n, R&& rng) requires std::convertible_to<std::ranges::range_reference_t<R>, T> {
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

				if constexpr ((sil & silence_t::silence_less) == 0) if (x < n) throw std::invalid_argument("too few arguments given to matrix(from_range, m, n, { ... })");

				if constexpr (fill_all) h.fill_default();
				else                    h.insert_default_n(n - i);
			}
		}

		template<silence_t sil, typename T, template <typename> typename holder, std::ranges::input_range R>
		constexpr inline T* create_from_range(std::size_t m, std::size_t n, R&& rng) requires std::convertible_to<std::ranges::range_reference_t<R>, T> {
			holder<T> h(m, n);
			take_n_from<sil, true>(h, m * n, std::forward<R>(rng));
			return h.release<true>();
		}

		template<silence_t sil, bool is_transpose, typename T, std::ranges::input_range R>
		constexpr inline T* create_from_range_range(std::size_t m, std::size_t n, R&& rng) 
			requires std::ranges::input_range<std::ranges::range_reference_t<R>> && 
			std::convertible_to<std::ranges::range_reference_t<std::ranges::range_reference_t<R>>, T> {

			std::conditional_t<is_transpose, _construction_holder_transpose<T>, _construction_holder<T>> h(m, n);
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
				if constexpr ((sil & silence_t::silence_less) == 0) if (x < m) throw std::invalid_argument("too few arguments given to matrix(from_range, m, n, { ... })");
				return h.release<true>();
			}
		}

#		endif
	}
	}






























}

#endif // !BHAVESH_MATRIX_H