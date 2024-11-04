#pragma once

#include <cstddef>
#include <utility>
#include <memory>
#include <stdexcept>
#include <complex>
#include <iterator>
#include <type_traits>
#include <ranges>

#if defined(_MSC_VER)
#define BHAVESH_CXX_VER _MSVC_LANG
#else
#define BHAVESH_CXX_VER __cplusplus
#endif

#if BHAVESH_CXX_VER >= 202002L
#define BHAVESH_CXX20_CONSTEXPR constexpr
#define BHAVESH_CXX20_VIEW : public std::ranges::view_base
#else
#define BHAVESH_CXX20_CONSTEXPR
#define BHAVESH_CXX20_VIEW
#endif

#if !defined(BHAVESH_NO_DEDUCE_DEBUG) && defined(_DEBUG) && !defined(NDEBUG) // works for both MSVC(_DEBUG) and gcc/cmake(NDEBUG)
#define BHAVESH_DEBUG
#endif

#ifdef BHAVESH_DEBUG
#define BHAVESH_USE_IF_DEBUG(...) __VA_ARGS__
#else
#define BHAVESH_USE_IF_DEBUG(...)
#endif

#if BHAVESH_CXX_VER > 202002L
#include <ranges>
#endif

namespace bhavesh {
	using std::size_t;
	using std::ptrdiff_t;

	constexpr struct transpose_t      { explicit constexpr transpose_t()      = default; } transpose;
	constexpr struct take_ownership_t { explicit constexpr take_ownership_t() = default; } take_ownership;

	namespace detail {
#if BHAVESH_CXX_VER >= 202002
		using std::construct_at;
		using std::destroy_at;
		using std::destroy_n;

		template <typename T>
		constexpr T* allocate(std::size_t n) {
			return std::allocator<T>{}.allocate(n);
		}

		template <typename T>
		constexpr void deallocate(T* p, std::size_t n) {
			std::allocator<T>{}.deallocate(p, n);
		}
#else
		template <typename T>
		T* allocate(std::size_t n) {
			return reinterpret_cast<T*>(::operator new(n * sizeof(T)));
		}

		template <typename T>
		void deallocate(T* p, std::size_t n) {
			::operator delete(p, n * sizeof(T));
		}

		template<typename T, class... Args>
		T* construct_at(T* p, Args&&... args) {
			return ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
		}
		
		template<typename T, typename=std::enable_if_t<std::is_array_v<T>>>
		void destroy_at(T* p) {
			for (auto& elem : *p) (destroy_at)(std::addressof(elem));
		}

		template<typename T, typename=std::enable_if_t<!std::is_array_v<T>>>
		void destroy_at(T* p) {
			p->~T();
		}

		template<typename T>
		ForwardIt destroy_n(T* first, size_t n)
		{
			for (; n > 0; (void) ++first, --n)
				std::destroy_at(std::addressof(*first));
			return first;
		}
#endif
		template <typename T>
		class partial_alloc {
		public:
			BHAVESH_CXX20_CONSTEXPR partial_alloc() : first(nullptr), capacity(0) {}
			BHAVESH_CXX20_CONSTEXPR partial_alloc(size_t s) : first((bhavesh::detail::allocate<T>)(s)), capacity(s) {}
			BHAVESH_CXX20_CONSTEXPR ~partial_alloc() {
				if (first) {
					(destroy_n)(first, size);
					(deallocate)(std::exchange(first, nullptr), capacity);
				}
			}
			template <typename ...tArgs>
			void emplace_back(tArgs&&... args) {
				construct_at(first + (size++), std::forward<tArgs>(args)...);
			}
			
			template<typename=std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>>
			void emplace_back() {
				construct_at(first + (size++), T());
			}

			T* release() {
#ifdef BHAVESH_DEBUG
				if (size != capacity) throw std::runtime_error("all values were not initialized");
#endif
				size = capacity = 0;
				return std::exchange(first, nullptr);
			}

		private:
			T* first;
			size_t size=0;
			size_t capacity;
		};


		template <typename T, typename=std::enable_if_t<std::is_floating_point_v<T> || std::is_integral_v<T>>>
		inline BHAVESH_CXX20_CONSTEXPR T* default_construct(T* p) {
			return construct_at(p, T());
		}
		template <typename T, std::enable_if_t<!std::is_floating_point_v<T> && !std::is_integral_v<T>, bool> = true>
		inline BHAVESH_CXX20_CONSTEXPR T* default_construct(T* p) {
			return construct_at(p);
		}


		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_default(size_t s) {
			T* data = (allocate<T>)(s);
			size_t i = 0;
			try {
				for (size_t i = 0; i < s; ++i) {
					(default_construct)(data + i);
				}
			}
			catch (...) {
				while (--i) (destroy_at)(data + i);
				if (s) destroy_at(data);
				(deallocate)(data, s);
			}
			return data;
		}

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_from_val(size_t s, const T& v) {
			T* data = (allocate<T>)(s);
			for (size_t i = 0; i < s; ++i) {
				(construct_at)(data + i, v);
			}
			return data;
		}

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_from_il(size_t s, std::initializer_list<T> il, bool silence_too_long) {
			if (il.size() > s) throw std::invalid_argument("too many inputs in initializer list for matrix");
			T* data = (allocate<T>)(s);
			for (size_t i = 0; i < il.size(); ++i) {
				(construct_at)(data + i, il.begin()[i]);
			}
			for (int i = il.size(); i < s; ++i) {
				(default_construct)(data + i);
			}
			return data;
		}

		template <typename T>
		inline BHAVESH_CXX20_CONSTEXPR T* create_from_il_transpose(size_t m, size_t n, std::initializer_list<T> il, bool silence_too_long) {
			if (il.size() > m * n) throw std::invalid_argument("too many inputs in initializer list for matrix");
			T* data = (allocate<T>)(m * n);
			for (size_t i = 0; i < m; ++i) {
				for (size_t j = 0; j < n; ++j) {
					if (j * m + i < il.size())
						(construct_at)(data + i * n + j, il.begin()[j * m + i]);
					else
						(default_construct)(data + i * n + j);
				}
			}
			return data;
		}

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_from_matrix(size_t s, const T* ptr) {
			T* data = (allocate<T>)(s);
			for (size_t i = 0; i < s; ++i) {
				(construct_at)(data + i, ptr[i]);
			}
			return data;
		}

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR T* create_transpose_from_matrix(const size_t m_orig, const size_t n_orig, const T* ptr) {
			T* data = (allocate<T>)(m_orig * n_orig);
			for (size_t i = 0; i < n_orig; ++i) {
				for (size_t j = 0; j < m_orig; ++j) {
					(construct_at)(data + i*m_orig+j, ptr[j*n_orig+i]);
				}
			}
			return data;
		}

		template <typename R, typename T>
		concept container_compatible_range = std::ranges::input_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;

#if BHAVESH_CXX_VER > 202002L
		template <typename T>
		constexpr T* create_from_range(size_t s, container_compatible_range<T> auto&& rng) {
			T* data = allocate<T>(s);
			size_t i = 0;
			for (auto&& x : rng) {
				construct_at(data + i++, x);
				if (i == s) break;
			}
			for (; i < s; ++i) {
				default_construct<T>(data + i);
			}
			return data;
		}
#endif


		template <typename T>
		BHAVESH_CXX20_CONSTEXPR void destroy_matrix(size_t s, T* data) {
			for (size_t i = 0; i < s; ++i) {
				(destroy_at)(data + i);
			}
			(deallocate)(data, s);
		}

		template <typename T>
		constexpr bool is_field_v = std::is_floating_point_v<T>;

		template <typename T>
		constexpr bool is_field_v<std::complex<T>> = is_field_v<T>;


	} // namespace detail

	template <typename T>
	class matrix_row {
	public:
		explicit BHAVESH_CXX20_CONSTEXPR matrix_row(T* p BHAVESH_USE_IF_DEBUG(, size_t n)) : BHAVESH_USE_IF_DEBUG(n(n),) m_data(p) {}
		BHAVESH_CXX20_CONSTEXPR T& operator[](size_t i) {
#ifdef BHAVESH_DEBUG
			if (i >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#endif
			return m_data[i];
		}

	private:
#ifdef BHAVESH_DEBUG
		size_t n;
#endif
		T* m_data;
	};
	
	template <typename T>
	class matrix_column {
	public:
		explicit BHAVESH_CXX20_CONSTEXPR matrix_column(T* p, BHAVESH_USE_IF_DEBUG(size_t m,) size_t n) : BHAVESH_USE_IF_DEBUG(m(m),) n(n), m_data(p) {}
		BHAVESH_CXX20_CONSTEXPR T& operator[](size_t i) {
#ifdef BHAVESH_DEBUG
			if (i >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#endif
			return m_data[i * n];
		}

	private:
#ifdef BHAVESH_DEBUG
		size_t m;
#endif
		size_t n;
		T* m_data;
	};
	
	namespace iterators {

		template <typename T>
		class matrix_rowwise_iterator {
		public:
			using value_type = matrix_row<T>;
			using size_type = size_t;
			using difference_type = ptrdiff_t;
			using reference = value_type&;
			using const_reference = const value_type&;
			using pointer = value_type*;
			using const_pointer = const value_type*;
			using iterator_tag = std::random_access_iterator_tag;

		public:
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator() : n(0), m_data(nullptr) {}
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator(size_t n, T* data) : n(n), m_data(m_data) {}

		public:

#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const matrix_rowwise_iterator& it) const { return m_data <=> it.m_data; }
			constexpr bool operator==(const matrix_rowwise_iterator&) const = default;
			constexpr bool operator!=(const matrix_rowwise_iterator&) const = default;
#else
			bool operator==(const matrix_rowwise_iterator& it) const { return m_data == it.m_data; }
			bool operator!=(const matrix_rowwise_iterator& it) const { return m_data != it.m_data; }
			bool operator< (const matrix_rowwise_iterator& it) const { return m_data <  it.m_data; }
			bool operator<=(const matrix_rowwise_iterator& it) const { return m_data <= it.m_data; }
			bool operator> (const matrix_rowwise_iterator& it) const { return m_data >  it.m_data; }
			bool operator>=(const matrix_rowwise_iterator& it) const { return m_data >= it.m_data; }
#endif

			BHAVESH_CXX20_CONSTEXPR value_type operator*() const {
				return value_type(m_data BHAVESH_USE_IF_DEBUG(, n));
			}
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator& operator++() {
				m_data += n;
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator operator++(int) {
				matrix_rowwise_iterator cpy = *this;
				m_data += n;
				return cpy;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator& operator--() {
				m_data -= n;
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator operator--(int) {
				matrix_rowwise_iterator cpy = *this;
				m_data -= n;
				return cpy;
			}

			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator& operator+= (ptrdiff_t n) { m_data += n * this->n; return *this; }
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator& operator-= (ptrdiff_t n) { m_data -= n * this->n; return *this; }

			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator operator+ (ptrdiff_t n) const { matrix_rowwise_iterator cpy = *this; cpy.m_data += n * this->n; return cpy; }
			BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator operator- (ptrdiff_t n) const { matrix_rowwise_iterator cpy = *this; cpy.m_data -= n * this->n; return cpy; }

			BHAVESH_CXX20_CONSTEXPR difference_type         operator- (const matrix_rowwise_iterator& it) const { return (m_data - it.m_data) / (difference_type)n; }

			BHAVESH_CXX20_CONSTEXPR value_type operator[](ptrdiff_t n) const {
				return value_type(m_data + n * this->n BHAVESH_USE_IF_DEBUG(, n));
			}
		private:
			size_t n;
			T* m_data;
		};

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR matrix_rowwise_iterator<T> operator+(ptrdiff_t n, const matrix_rowwise_iterator<T>&it) {
			return it + n;
		}

		template <typename T>
		class matrix_rowwise_iterable BHAVESH_CXX20_VIEW {
		public:
			using iterator = matrix_rowwise_iterator<T>;
			using const_iterator = matrix_rowwise_iterator<const T>;
			using reverse_iterator = std::reverse_iterator<iterator>;
			using const_reverse_iterator = std::reverse_iterator<const_iterator>;

			matrix_rowwise_iterable(size_t m, size_t n, T* data) : m(m), n(n), m_data(data) {}

			iterator begin() { return iterator{ n, m_data };         }
			iterator end()   { return iterator{ n, m_data + m * n }; }

			const_iterator begin() const { return const_iterator{ n, m_data };         }
			const_iterator end()   const { return const_iterator{ n, m_data + m * n }; }

			const_iterator cbegin() const { return begin(); }
			const_iterator cend()   const { return end();   }

			reverse_iterator rbegin() { return reverse_iterator(end());   }
			reverse_iterator rend()   { return reverse_iterator(begin()); }

			const_reverse_iterator rbegin() const { return const_reverse_iterator(end());   }
			const_reverse_iterator rend()   const { return const_reverse_iterator(begin()); }

			const_reverse_iterator crbegin() const { return rbegin(); }
			const_reverse_iterator crend()   const { return rend();   }

		private:
			size_t m, n;
			T* m_data;
		};


		template <typename T>
		class matrix_colwise_iterator {
		public:
			using value_type = matrix_column<T>;
			using size_type = size_t;
			using difference_type = ptrdiff_t;
			using reference = value_type&;
			using const_reference = const value_type&;
			using pointer = value_type*;
			using const_pointer = const value_type*;
			using iterator_tag = std::random_access_iterator_tag;

		public:
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator() : BHAVESH_USE_IF_DEBUG(m(0),) n(0), m_data(nullptr) {}
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator(BHAVESH_USE_IF_DEBUG(size_t m,) size_t n, T* data) : BHAVESH_USE_IF_DEBUG(m(m),) n(n), m_data(m_data) {}

		public:
#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const matrix_colwise_iterator& it) const { return m_data <=> it.m_data; }
			constexpr bool operator==(const matrix_colwise_iterator&) const = default;
			constexpr bool operator!=(const matrix_colwise_iterator&) const = default;
#else
			bool operator==(const matrix_colwise_iterator& it) const { return m_data == it.m_data; }
			bool operator!=(const matrix_colwise_iterator& it) const { return m_data != it.m_data; }
			bool operator< (const matrix_colwise_iterator& it) const { return m_data <  it.m_data; }
			bool operator<=(const matrix_colwise_iterator& it) const { return m_data <= it.m_data; }
			bool operator> (const matrix_colwise_iterator& it) const { return m_data >  it.m_data; }
			bool operator>=(const matrix_colwise_iterator& it) const { return m_data >= it.m_data; }
#endif
			BHAVESH_CXX20_CONSTEXPR value_type operator*() const {
				return value_type(m_data, BHAVESH_USE_IF_DEBUG(m,) n);
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator& operator++() {
				m_data++;
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator operator++(int) {
				matrix_colwise_iterator cpy = *this;
				m_data++;
				return cpy;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator& operator--() {
				m_data--;
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator operator--(int) {
				matrix_colwise_iterator cpy = *this;
				m_data--;
				return cpy;
			}

			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator& operator+= (ptrdiff_t n) { m_data += n; return *this; }
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator& operator-= (ptrdiff_t n) { m_data -= n; return *this; }

			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator operator+ (ptrdiff_t n) const { matrix_colwise_iterator cpy = *this; cpy.m_data += n; return cpy; }
			BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator operator- (ptrdiff_t n) const { matrix_colwise_iterator cpy = *this; cpy.m_data -= n; return cpy; }

			BHAVESH_CXX20_CONSTEXPR difference_type         operator- (const matrix_colwise_iterator& it) const { return m_data - it.m_data; }

			BHAVESH_CXX20_CONSTEXPR value_type operator[](ptrdiff_t n) const {
				return value_type(m_data + n, BHAVESH_USE_IF_DEBUG(m,) n);
			}
		private:
#ifdef BHAVESH_DEBUG
			size_t m;
#endif;
			size_t n;
			T* m_data;
		};

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR matrix_colwise_iterator<T> operator+(ptrdiff_t n, const matrix_colwise_iterator<T>& it) {
			return it + n;
		}

		template <typename T>
		class matrix_colwise_iterable BHAVESH_CXX20_VIEW {
		public:
			using iterator = matrix_colwise_iterator<T>;
			using const_iterator = matrix_colwise_iterator<const T>;
			using reverse_iterator = std::reverse_iterator<iterator>;
			using const_reverse_iterator = std::reverse_iterator<const_iterator>;

			matrix_colwise_iterable(size_t m, size_t n, T* data) : BHAVESH_USE_IF_DEBUG(m(m), ) n(n), m_data(data) {}

			iterator begin() { return iterator{ BHAVESH_USE_IF_DEBUG(m,) n, m_data };     }
			iterator end()   { return iterator{ BHAVESH_USE_IF_DEBUG(m,) n, m_data + n }; }

			const_iterator begin() const { return const_iterator{ BHAVESH_USE_IF_DEBUG(m,) n, m_data };     }
			const_iterator end()   const { return const_iterator{ BHAVESH_USE_IF_DEBUG(m,) n, m_data + n }; }

			const_iterator cbegin() const { return begin(); }
			const_iterator cend()   const { return end();   }

			reverse_iterator rbegin() { return reverse_iterator(end());   }
			reverse_iterator rend()   { return reverse_iterator(begin()); }

			const_reverse_iterator rbegin() const { return const_reverse_iterator(end());   }
			const_reverse_iterator rend()   const { return const_reverse_iterator(begin()); }

			const_reverse_iterator crbegin() const { return rbegin(); }
			const_reverse_iterator crend()   const { return rend();   }

		private:
#ifdef BHAVESH_DEBUG
			size_t m;
#endif
			size_t n;
			T* m_data;
		};


		template <typename T>
		class matrix_rowmajor_iterator {
		public:
			using value_type = T;
			using size_type = size_t;
			using difference_type = ptrdiff_t;
			using reference = value_type&;
			using const_reference = const value_type&;
			using pointer = value_type*;
			using const_pointer = const value_type*;
#if BHAVESH_CXX_VER >= 202002L
			using iterator_tag = std::contiguous_iterator_tag;
#else
			using iterator_tag = std::random_access_iterator_tag;
#endif
		public:
			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator() : m_data(nullptr) {}
			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator(T* data) : m_data(m_data) {}

		public:

#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const matrix_rowmajor_iterator& it) const { return m_data <=> it.m_data; }
			constexpr bool operator==(const matrix_rowmajor_iterator&) const = default;
			constexpr bool operator!=(const matrix_rowmajor_iterator&) const = default;
#else
			bool operator==(const matrix_rowmajor_iterator& it) const { return m_data == it.m_data; }
			bool operator!=(const matrix_rowmajor_iterator& it) const { return m_data != it.m_data; }
			bool operator< (const matrix_rowmajor_iterator& it) const { return m_data < it.m_data;  }
			bool operator<=(const matrix_rowmajor_iterator& it) const { return m_data <= it.m_data; }
			bool operator> (const matrix_rowmajor_iterator& it) const { return m_data > it.m_data;  }
			bool operator>=(const matrix_rowmajor_iterator& it) const { return m_data >= it.m_data; }
#endif

			BHAVESH_CXX20_CONSTEXPR reference operator*() const {
				return *m_data;
			}
			BHAVESH_CXX20_CONSTEXPR pointer operator->() const {
				return m_data;
			}

			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator& operator++() {
				m_data++;
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator operator++(int) {
				matrix_rowmajor_iterator cpy = *this;
				m_data++;
				return cpy;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator& operator--() {
				m_data--;
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator operator--(int) {
				matrix_rowmajor_iterator cpy = *this;
				m_data--;
				return cpy;
			}

			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator& operator+= (ptrdiff_t n) { m_data += n; return *this; }
			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator& operator-= (ptrdiff_t n) { m_data -= n; return *this; }

			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator operator+ (ptrdiff_t n) const { matrix_rowmajor_iterator cpy = *this; cpy.m_data += n; return cpy; }
			BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator operator- (ptrdiff_t n) const { matrix_rowmajor_iterator cpy = *this; cpy.m_data -= n; return cpy; }

			BHAVESH_CXX20_CONSTEXPR difference_type          operator- (const matrix_rowmajor_iterator& it) const { return m_data - it.m_data; }

			BHAVESH_CXX20_CONSTEXPR reference operator[](ptrdiff_t n) const {
				return m_data[n];
			}
		private:
			T* m_data;
		};

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR matrix_rowmajor_iterator<T> operator+(ptrdiff_t n, const matrix_rowmajor_iterator<T>& it) {
			return it + n;
		}

		template <typename T>
		class matrix_rowmajor_iterable BHAVESH_CXX20_VIEW {
		public:
			using iterator = matrix_rowmajor_iterator<T>;
			using const_iterator = matrix_rowmajor_iterator<const T>;
			using reverse_iterator = std::reverse_iterator<iterator>;
			using const_reverse_iterator = std::reverse_iterator<const_iterator>;

			matrix_rowmajor_iterable(size_t m, size_t n, T* data) : s(m * n), m_data(data) {}

			iterator begin() { return iterator{ m_data };     }
			iterator end()   { return iterator{ m_data + s }; }

			const_iterator begin() const { return const_iterator{ m_data };     }
			const_iterator end()   const { return const_iterator{ m_data + s }; }

			const_iterator cbegin() const { return begin(); }
			const_iterator cend()   const { return end();   }

			reverse_iterator rbegin() { return reverse_iterator(end());   }
			reverse_iterator rend()   { return reverse_iterator(begin()); }

			const_reverse_iterator rbegin() const { return const_reverse_iterator(end());   }
			const_reverse_iterator rend()   const { return const_reverse_iterator(begin()); }

			const_reverse_iterator crbegin() const { return rbegin(); }
			const_reverse_iterator crend()   const { return rend();   }

		private:
			size_t s;
			T* m_data;
		};
		

		template <typename T>
		class matrix_colmajor_iterator {
		public:
			using value_type = T;
			using size_type = size_t;
			using difference_type = ptrdiff_t;
			using reference = value_type&;
			using const_reference = const value_type&;
			using pointer = value_type*;
			using const_pointer = const value_type*;
			using iterator_tag = std::random_access_iterator_tag;

		public:
			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator() : m(0), n(0), m_data(nullptr) {}
			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator(size_t m, size_t n, T* data) : m(m), n(n), m_data(m_data) {}

		public:

#if BHAVESH_CXX_VER >= 202002L
			constexpr std::strong_ordering operator<=>(const matrix_colmajor_iterator& it) const { std::strong_ordering x = m_data <=> it.m_data; return (x != std::strong_ordering::equal) ? x : (i <=> it.i); }
			constexpr bool operator==(const matrix_colmajor_iterator&) const = default;
			constexpr bool operator!=(const matrix_colmajor_iterator&) const = default;
#else
			bool operator==(const matrix_colmajor_iterator& it) const { return m_data == it.m_data && i == it.i; }
			bool operator!=(const matrix_colmajor_iterator& it) const { return m_data != it.m_data || i != it.i; }
			bool operator< (const matrix_colmajor_iterator& it) const { return m_data <= it.m_data && i <  it.i; }
			bool operator<=(const matrix_colmajor_iterator& it) const { return m_data <= it.m_data && i <= it.i; }
			bool operator> (const matrix_colmajor_iterator& it) const { return m_data >= it.m_data && i >  it.i; }
			bool operator>=(const matrix_colmajor_iterator& it) const { return m_data >= it.m_data && i >= it.i; }
#endif

			BHAVESH_CXX20_CONSTEXPR reference operator*() const {
				return m_data[n*i];
			}
			BHAVESH_CXX20_CONSTEXPR pointer operator->() const {
				return m_data + n*i;
			}

			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator& operator++() {
				if (i == m - 1) { i = 0; m_data++; }
				else            { i++; }
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator operator++(int) {
				matrix_colmajor_iterator cpy = *this;
				++*this;
				return cpy;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator& operator--() {
				if (!i) { i = m - 1; m_data--; }
				else    { i--; }
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator operator--(int) {
				matrix_colmajor_iterator cpy = *this;
				--*this;
				return cpy;
			}

			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator& operator+= (ptrdiff_t n) { 
				i += n;
				m_data += i / m;
				i %= m;
				if (i < 0) { i += m; m_data--; }
				return *this;
			}
			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator& operator-= (ptrdiff_t n) { *this += (-n); return *this; }

			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator operator+ (ptrdiff_t n) const { matrix_colmajor_iterator cpy = *this; cpy += n; return cpy; }
			BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator operator- (ptrdiff_t n) const { matrix_colmajor_iterator cpy = *this; cpy -= n; return cpy; }

			BHAVESH_CXX20_CONSTEXPR difference_type          operator- (const matrix_colmajor_iterator& it) const { return (m_data - it.m_data)*m + i-it.i; }

			BHAVESH_CXX20_CONSTEXPR reference operator[](ptrdiff_t n) const {
				return *(*this + n);
			}
		private:
			size_t m, n;
			ptrdiff_t i = 0;
			T* m_data;
		};

		template <typename T>
		BHAVESH_CXX20_CONSTEXPR matrix_colmajor_iterator<T> operator+(ptrdiff_t n, const matrix_colmajor_iterator<T>& it) {
			return it + n;
		}

		template <typename T>
		class matrix_colmajor_iterable BHAVESH_CXX20_VIEW {
		public:
			using iterator = matrix_colmajor_iterator<T>;
			using const_iterator = matrix_colmajor_iterator<const T>;
			using reverse_iterator = std::reverse_iterator<iterator>;
			using const_reverse_iterator = std::reverse_iterator<const_iterator>;

			matrix_colmajor_iterable(size_t m, size_t n, T* data) : m(m), n(n), m_data(data) {}

			iterator begin() { return iterator{ m, n, m_data }; }
			iterator end()   { return iterator{ m, n, m_data+n }; }

			const_iterator begin() const { return const_iterator{ m, n, m_data }; }
			const_iterator end()   const { return const_iterator{ m, n, m_data + n }; }

			const_iterator cbegin() const { return begin(); }
			const_iterator cend()   const { return end();   }

			reverse_iterator rbegin() { return reverse_iterator(end());   }
			reverse_iterator rend()   { return reverse_iterator(begin()); }

			const_reverse_iterator rbegin() const { return const_reverse_iterator(end());   }
			const_reverse_iterator rend()   const { return const_reverse_iterator(begin()); }

			const_reverse_iterator crbegin() const { return rbegin(); }
			const_reverse_iterator crend()   const { return rend();   }

		private:
			size_t m, n;
			T* m_data;
		};

	} // namespace iterators

	template <typename T, bool is_field=detail::is_field_v<T>,
		typename RowWise=iterators::matrix_rowwise_iterable<T>, typename ColWise=iterators::matrix_colwise_iterable<T>,
		typename RowMajor=iterators::matrix_rowmajor_iterable<T>, typename ColMajor=iterators::matrix_colmajor_iterable<T>
	>
	struct matrix_traits {
		constexpr static bool is_field_v = is_field;

		using rowwise_iterable = RowWise;
		using rowwise_iterator = typename rowwise_iterable::iterator;
		using rowwise_const_iterator = typename rowwise_iterable::const_iterator;

		using colwise_iterable = ColWise;
		using colwise_iterator = typename colwise_iterable::iterator;
		using colwise_const_iterator = typename colwise_iterable::const_iterator;

		using rowmajor_iterable = RowMajor;
		using rowmajor_iterator = typename rowmajor_iterable::iterator;
		using rowmajor_const_iterator = typename rowmajor_iterable::const_iterator;

		using colmajor_iterable = ColMajor;
		using colmajor_iterator = typename colmajor_iterable::iterator;
		using colmajor_const_iterator = typename colmajor_iterable::const_iterator;

	};

	template <typename T, typename Traits = matrix_traits<T>>
	class matrix {
		using inverse_type = std::conditional_t<std::is_integral_v<T>, matrix<double>, matrix<T>>;
	public:
		static_assert(!std::is_reference_v<T>, "matrix of reference type is ill-defined");
		static_assert(!std::is_const_v<T>, "matrix<const T> is ill-defined; use const matrix<T> instead");
		static_assert(std::is_nothrow_destructible_v<T>, "NEVER THROW IN YOUR DESTRUCTORS!!!");

		using value_type = T;
		using traits_type = Traits;
		using size_type = size_t;
		using reference = value_type&;
		using const_reference = const value_type&;
		using pointer = value_type*;
		using const_pointer = const value_type*;
		
		constexpr static bool is_field_v = traits_type::is_field_v;
		
		using rowwise_iterable = traits_type::rowwise_iterable;
		using rowwise_iterator = traits_type::rowwise_iterator;
		using rowwise_const_iterator = traits_type::rowwise_const_iterator;

		using colwise_iterable = traits_type::colwise_iterable;
		using colwise_iterator = traits_type::colwise_iterator;
		using colwise_const_iterator = traits_type::colwise_const_iterator;

		using rowmajor_iterable = traits_type::rowmajor_iterable;
		using rowmajor_iterator = traits_type::rowmajor_iterator;
		using rowmajor_const_iterator = traits_type::rowmajor_const_iterator;

		using colmajor_iterable = traits_type::colmajor_iterable;
		using colmajor_iterator = traits_type::colmajor_iterator;
		using colmajor_const_iterator = traits_type::colmajor_const_iterator;

	public:
		BHAVESH_CXX20_CONSTEXPR matrix() : m(0), n(0), m_data(nullptr) {}
		
		BHAVESH_CXX20_CONSTEXPR matrix(size_t m, size_t n) : m(m), n(n), m_data(detail::create_default<T>(m*n)) {}
		BHAVESH_CXX20_CONSTEXPR matrix(size_t m, size_t n, const T& v) : m(m), n(n), m_data(detail::create_from_val(m * n, v)) {}
		
		BHAVESH_CXX20_CONSTEXPR matrix(size_t m, size_t n, std::initializer_list<T> il, bool silence_too_long=false) : m(m), n(n), m_data(detail::create_from_il(m * n, il, silence_too_long)) {}
		BHAVESH_CXX20_CONSTEXPR matrix(transpose_t, size_t m, size_t n, std::initializer_list<T> il, bool silence_too_long=false) : m(m), n(n), m_data(detail::create_from_il_transpose(m, n, il, silence_too_long)) {}
		
		BHAVESH_CXX20_CONSTEXPR matrix(const matrix& mat) : m(mat.m), n(mat.n), m_data(detail::create_from_matrix(mat.m * mat.n, mat.m_data)) {}
		BHAVESH_CXX20_CONSTEXPR matrix(transpose_t, const matrix& mat) : m(mat.n), n(mat.m), m_data(detail::create_transpose_from_matrix(mat.m, mat.n, mat.m_data)) {}
		
		BHAVESH_CXX20_CONSTEXPR matrix(matrix&& mat) noexcept : m(std::exchange(mat.m, 0)), n(std::exchange(mat.n, 0)), m_data(std::exchange(mat.m_data, nullptr)) {}

		BHAVESH_CXX20_CONSTEXPR explicit matrix(take_ownership_t, size_t m, size_t n, T* &data) : m(m), n(n), m_data(std::exchange(data, nullptr)) {}
		BHAVESH_CXX20_CONSTEXPR explicit matrix(take_ownership_t, size_t m, size_t n, T* &&data) : m(m), n(n), m_data(std::exchange(data, nullptr)) {}

#if BHAVESH_CXX_VER > 202002L
		template <detail::container_compatible_range<T> RNG>
		constexpr matrix(std::from_range_t, size_t m, size_t n, RNG&& rng) : m(m), n(n), m_data(detail::create_from_range(m * n, std::forward<RNG>(rng))) {}
#endif

		BHAVESH_CXX20_CONSTEXPR ~matrix() {
			if (m_data) {
				detail::destroy_matrix(m * n, m_data);
#ifdef BHAVESH_DEBUG
				m_data = nullptr;
				m = n = 0;
#endif
			}
		}
		


		BHAVESH_CXX20_CONSTEXPR matrix& operator=(const matrix& oth) {
			const size_t s = oth.m * oth.n;
			if (oth.m * oth.n == m * n) {
				for (size_t i = 0; i < s; ++i) {
					m_data[i] = oth.m_data[i];
				}
			}
			else {
				detail::destroy_matrix(m * n, m_data);
				m_data = detail::create_from_matrix(s, oth.m_data);
			}
			m = oth.m;
			n = oth.n;
			return *this;
		}
		
		BHAVESH_CXX20_CONSTEXPR matrix& operator=(matrix&& oth) noexcept {
			m = std::exchange(oth.m, 0);
			n = std::exchange(oth.n, 0);
			m_data = std::exchange(oth.m_data, nullptr);
			return *this;
		}



		BHAVESH_CXX20_CONSTEXPR bool operator!=(const matrix& oth) {
			if (oth.m != m || oth.n != n) return true;
			const size_t s = m * n;
			for (size_t i = 0; i < s; ++i) {
				if (m_data[i] != oth.m_data[i]) return true;
			}
			return false;
		}
		BHAVESH_CXX20_CONSTEXPR bool operator==(const matrix& oth) { return !(*this != oth); }


		
		BHAVESH_CXX20_CONSTEXPR matrix transpose() const {
			return matrix(bhavesh::transpose, *this);
		}
		BHAVESH_CXX20_CONSTEXPR matrix& transpose_inplace() {
			// WARNING: MOVES TO NEW MATRIX AND THEN MOVES IT BACK; USES sizeof(T)*m*n + O(1) MEMORY IF M != N; USE transpose() IF YOU WANT A COPY
			if (m == n) {
				for (size_t i = 0; i < n; ++i) {
					for (size_t j = i + 1; j < n; ++j) {
						std::swap(m_data[i * n + j], m_data[j * n + i]);
					}
				}
			}
			else {
				_transpose_inplace_impl();
			}
			return *this;
		}


		BHAVESH_CXX20_CONSTEXPR matrix_row<T> operator[](size_t idx) {
#ifdef BHAVESH_DEBUG
			if (idx >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return matrix_row<T>(m_data + idx * n, n);
#else
			return matrix_row<T>(m_data + idx * n);
#endif
		}

		BHAVESH_CXX20_CONSTEXPR matrix_row<const T> operator[](size_t idx) const {
#ifdef BHAVESH_DEBUG
			if (idx >= m) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
			return matrix_row<const T>(m_data + idx * n, n);
#else
			return matrix_row<const T>(m_data + idx * n);
#endif
		}

		
		
		BHAVESH_CXX20_CONSTEXPR T& operator()(size_t i, size_t j) {
#ifdef BHAVESH_DEBUG
			if (i >= m || j >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#endif
			return m_data[i * n + j];
		}

		BHAVESH_CXX20_CONSTEXPR const T& operator()(size_t i, size_t j) const {
#ifdef BHAVESH_DEBUG
			if (i >= m || j >= n) throw std::out_of_range("Out of range element access attempted for matrix[i][j]");
#endif
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

		BHAVESH_CXX20_CONSTEXPR T get_default(size_t i, size_t j, T default_value = T{}) const noexcept {
			if (i >= m || j >= n) return std::move(default_value);
			return m_data[i * n + j];
		}



		BHAVESH_CXX20_CONSTEXPR matrix operator+(const matrix& oth) const& {
			if (oth.m != m || oth.n != n) throw std::invalid_argument("Addition of matrices requires same shape");
			matrix answer = matrix<T>(*this);
			for (int i = 0; i < m * n; ++i) {
				answer.m_data[i] += oth.m_data[i];
			}
			return answer;
		}

		BHAVESH_CXX20_CONSTEXPR matrix&& operator+(matrix&& oth) const& {
			if (oth.m != m || oth.n != n) throw std::invalid_argument("Addition of matrices requires same shape");
			for (int i = 0; i < m * n; ++i) {
				oth.m_data[i] = m_data[i] + oth.m_data[i];
			}
			return std::move(oth);
		}
		
		BHAVESH_CXX20_CONSTEXPR matrix& operator+=(const matrix& oth) {
			if (oth.m != m || oth.n != n) throw std::invalid_argument("Addition of matrices requires same shape");
			for (int i = 0; i < m * n; ++i) {
				m_data[i] += oth.m_data[i];
			}
			return *this;
		}

		BHAVESH_CXX20_CONSTEXPR matrix&& operator+(const matrix& oth) && {
			return std::move(*this += oth);
		}



		BHAVESH_CXX20_CONSTEXPR matrix operator-(const matrix& oth) const& {
			if (oth.m != m || oth.n != n) throw std::invalid_argument("Addition of matrices requires same shape");
			matrix answer = matrix<T>(*this);
			for (int i = 0; i < m * n; ++i) {
				answer.m_data[i] -= oth.m_data[i];
			}
			return answer;
		}

		BHAVESH_CXX20_CONSTEXPR matrix&& operator-(matrix&& oth) const& {
			if (oth.m != m || oth.n != n) throw std::invalid_argument("Addition of matrices requires same shape");
			for (int i = 0; i < m * n; ++i) {
				oth.m_data[i] = m_data[i] - oth.m_data[i];
			}
			return std::move(oth);
		}

		BHAVESH_CXX20_CONSTEXPR matrix& operator-=(const matrix& oth) {
			if (oth.m != m || oth.n != n) throw std::invalid_argument("Addition of matrices requires same shape");
			for (int i = 0; i < m * n; ++i) {
				m_data[i] -= oth.m_data[i];
			}
			return *this;
		}

		BHAVESH_CXX20_CONSTEXPR matrix&& operator-(const matrix& oth) && {
			return std::move(*this -= oth);
		}

		BHAVESH_CXX20_CONSTEXPR matrix operator*(const matrix& oth) const {

		}

		
	private:
		BHAVESH_CXX20_CONSTEXPR void _transpose_inplace_impl() {
			T* cpy = detail::allocate<T>(m*n);
			for (size_t i = 0; i < n; ++i) {
				for (size_t j = 0; j < m; ++j) {
					(detail::construct_at)(cpy + i * m + j, std::move(m_data[j * n + i]));
				}
			}
			const size_t s = m * n;
			for (size_t i = 0; i < s; ++i) {
				m_data[i] = std::move(cpy[i]);
			}
			detail::destroy_matrix(m * n, cpy);
			std::swap(m, n);
		}

	private:
		size_t m, n; // m × n matrix
		T* m_data;
	};

} // namespace bhavesh