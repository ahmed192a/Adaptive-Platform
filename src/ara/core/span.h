/**
 * @file span.h
 * @brief ara::core::Span (SWS_CORE_01901 family): a non-owning view of a contiguous sequence.
 *
 * Complete implementation (the previous header only declared the interface and did not compile under
 * -Werror because of the deprecated std::iterator). Iterators are plain pointers, which satisfy the
 * RandomAccess/Contiguous iterator requirements. See doc/spec-conformance/core.md.
 */
#ifndef ARA_CORE_SPAN_H_
#define ARA_CORE_SPAN_H_

#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ara {
namespace core {

constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

template <typename T, std::size_t Extent = dynamic_extent>
class Span {
 public:
  using element_type = T;
  using value_type = typename std::remove_cv<T>::type;
  using index_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = T const *;
  using reference = T &;
  using const_reference = T const &;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  static constexpr index_type extent = Extent;

  // Only the dynamic extent may be default constructed / empty.
  template <std::size_t E = Extent, typename = typename std::enable_if<E == dynamic_extent || E == 0>::type>
  constexpr Span() noexcept : ptr_(nullptr), size_(0) {}

  constexpr Span(pointer ptr, index_type count) : ptr_(ptr), size_(count) {}
  constexpr Span(pointer first, pointer last) : ptr_(first), size_(static_cast<index_type>(last - first)) {}

  template <std::size_t N>
  constexpr Span(element_type (&arr)[N]) noexcept : ptr_(arr), size_(N) {}
  template <std::size_t N>
  constexpr Span(std::array<value_type, N> &arr) noexcept : ptr_(arr.data()), size_(N) {}
  template <std::size_t N>
  constexpr Span(std::array<value_type, N> const &arr) noexcept : ptr_(arr.data()), size_(N) {}

  template <typename Container,
            typename = typename std::enable_if<
                !std::is_array<Container>::value &&
                std::is_convertible<decltype(std::declval<Container &>().data()), pointer>::value>::type>
  constexpr Span(Container &cont) : ptr_(cont.data()), size_(cont.size()) {}
  template <typename Container,
            typename = typename std::enable_if<
                std::is_const<element_type>::value && !std::is_array<Container>::value &&
                std::is_convertible<decltype(std::declval<Container const &>().data()), pointer>::value>::type>
  constexpr Span(Container const &cont) : ptr_(cont.data()), size_(cont.size()) {}

  constexpr Span(Span const &other) noexcept = default;
  template <typename U, std::size_t N,
            typename = typename std::enable_if<std::is_convertible<U (*)[], T (*)[]>::value>::type>
  constexpr Span(Span<U, N> const &s) noexcept : ptr_(s.data()), size_(s.size()) {}
  ~Span() noexcept = default;

  Span &operator=(Span const &other) noexcept = default;

  template <std::size_t Count>
  constexpr Span<element_type, Count> first() const { return Span<element_type, Count>(ptr_, Count); }
  constexpr Span<element_type, dynamic_extent> first(index_type count) const {
    return Span<element_type, dynamic_extent>(ptr_, count);
  }
  template <std::size_t Count>
  constexpr Span<element_type, Count> last() const { return Span<element_type, Count>(ptr_ + (size_ - Count), Count); }
  constexpr Span<element_type, dynamic_extent> last(index_type count) const {
    return Span<element_type, dynamic_extent>(ptr_ + (size_ - count), count);
  }
  template <std::size_t Offset, std::size_t Count = dynamic_extent>
  constexpr Span<element_type, Count> subspan() const {
    return Span<element_type, Count>(ptr_ + Offset, Count == dynamic_extent ? size_ - Offset : Count);
  }
  constexpr Span<element_type, dynamic_extent> subspan(index_type offset, index_type count = dynamic_extent) const {
    return Span<element_type, dynamic_extent>(ptr_ + offset, count == dynamic_extent ? size_ - offset : count);
  }

  constexpr index_type size() const noexcept { return size_; }
  constexpr index_type size_bytes() const noexcept { return size_ * sizeof(element_type); }
  constexpr bool empty() const noexcept { return size_ == 0; }

  constexpr reference operator[](index_type idx) const { return ptr_[idx]; }
  constexpr pointer data() const noexcept { return ptr_; }

  constexpr iterator begin() const noexcept { return ptr_; }
  constexpr iterator end() const noexcept { return ptr_ + size_; }
  constexpr const_iterator cbegin() const noexcept { return ptr_; }
  constexpr const_iterator cend() const noexcept { return ptr_ + size_; }
  reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
  reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

 private:
  pointer ptr_;
  index_type size_;
};

template <typename T>
constexpr Span<T> MakeSpan(T *ptr, typename Span<T>::index_type count) { return Span<T>(ptr, count); }
template <typename T>
constexpr Span<T> MakeSpan(T *first, T *last) { return Span<T>(first, last); }
template <typename T, std::size_t N>
constexpr Span<T, N> MakeSpan(T (&arr)[N]) noexcept { return Span<T, N>(arr); }
template <typename Container>
constexpr Span<typename Container::value_type> MakeSpan(Container &cont) {
  return Span<typename Container::value_type>(cont);
}
template <typename Container>
constexpr Span<typename Container::value_type const> MakeSpan(Container const &cont) {
  return Span<typename Container::value_type const>(cont);
}

}  // namespace core
}  // namespace ara

#endif  // ARA_CORE_SPAN_H_
