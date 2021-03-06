#ifndef METASIM_DATA_CONTAINER_HPP
#define METASIM_DATA_CONTAINER_HPP

#include "Core/data_array.hpp"
#include "Utils/logger.hpp"
#include <functional>
#include <set>
#include <tuple>
#include <unordered_map>


namespace MS {

/* type handler */
template<typename T>
struct TypeTag {
  std::string type_name;
  size_t type_hash;

  TypeTag(const char* type_name)
    : type_name(type_name)
    , type_hash(std::hash<std::string>()(type_name)) {}

  TypeTag(const std::string& type_name)
    : type_name(type_name)
    , type_hash(std::hash<std::string>()(type_name)) {}
};

// template <typename... Types> class DataContainerIterator;
template<typename... Types>
class DataSubset;

template<typename... Types>
class DataSubsetIterator;

// Dataset for manifolds attributes
class DataContainer {
public:
  // number of entry
  int total_size;
  std::unordered_map<size_t, std::unique_ptr<DataArrayBase>> dataset;

  template<typename Type>
  DataArray<Type>& append(const TypeTag<Type>& attr_tag, const Range& range,
                          const Type& init_value) {
    return append<Type>(attr_tag, range, std::vector<Type>(range.length(), init_value));
  }

  // TODO: return optional references
  template<typename Type>
  DataArray<Type>& get_array(const TypeTag<Type>& attr_tag) {
    auto iter = dataset.find(attr_tag.type_hash);
    return static_cast<DataArray<Type>&>(*iter->second);
  }

  template<typename Type>
  DataArray<Type>& append(const TypeTag<Type>& attr_tag, const Range& range,
                          std::vector<Type>&& array) {
    total_size = std::max(range.upper, total_size);
    auto iter = dataset.find(attr_tag.type_hash);

    if (iter == dataset.end()) {
      // if attribute array not found
      auto ret = dataset.emplace(
        attr_tag.type_hash,
        std::make_unique<DataArray<Type>>(attr_tag.type_name, RangeSet{range}, std::move(array)));
      return static_cast<DataArray<Type>&>(*ret.first->second);
    } else {
      // update data_array with range
      auto& old = static_cast<DataArray<Type>&>(*iter->second);
      // @TODO new range must at end of ranges ?
      old.append(range, std::forward<std::vector<Type>&&>(array));
      return old;
    }
  }

  //  template <typename... Types>
  //  DataContainerIterator<Types...>
  //  SubsetIterator(const TypeTag<Types> &...tags) {
  //    return DataContainerIterator<Types...>(get_array(tags)...);
  //  }

  template<typename... Types>
  DataSubset<Types...> Subset(const TypeTag<Types>&... tags) {
    return {get_array(tags)...};
  }

  template<typename... Types>
  DataSubset<Types...> Subset(const RangeSet& sub_ranges, const TypeTag<Types>&... tags) {
    return {sub_ranges, get_array(tags)...};
  }
};


// Data subset accessor, can't edit
template<typename... Types>
class DataSubset {
public:
  using array_pack_reference = std::tuple<DataArray<Types>&...>;
  using iterators_type = std::tuple<typename DataArray<Types>::iterator...>;
  using size_type = std::size_t;

  using iterator = DataSubsetIterator<Types...>;
  using const_iterator = DataSubsetIterator<Types...>;

  // array_pack's common ranges by default
  RangeSet sub_ranges;
  // each array set contains its own ranges;
  array_pack_reference array_pack;
  // std::vector<size_t> entry2index;
  // std::vector<size_t> index2entry;

  DataSubset() = default;

  DataSubset(DataArray<Types>&... array_pack)
    : sub_ranges(array_pack.ranges...)
    , array_pack(array_pack...) {}

  DataSubset(const RangeSet& sub_ranges, DataArray<Types>&... array_pack)
    : sub_ranges(sub_ranges, array_pack.ranges...)
    , array_pack(array_pack...) {}

  // shrink
  DataSubset(const DataSubset& other) = default;

  DataSubset(DataSubset& other, tbb::split)
    : sub_ranges(other.sub_ranges, tbb::split{})
    , array_pack(other.array_pack) {}

  bool is_divisible() const { return sub_ranges.is_divisible(); }
  bool empty() const { return sub_ranges.empty(); }
  // split data_subset
  auto size() const { return sub_ranges.length(); }

  auto array_pack_begins() {
    return std::apply([](auto&&... args) { return iterators_type(args.begin()...); }, array_pack);
  }
  auto array_pack_ends() {
    return std::apply([](auto&&... args) { return iterators_type(args.end()...); }, array_pack);
  }


  auto begin() { return iterator(array_pack_begins(), sub_ranges.begin(), 0); }
  auto begin() const { return const_iterator(array_pack_begins(), sub_ranges.begin(), 0); }
  auto end() { return iterator(array_pack_ends(), sub_ranges.end(), 0); }
  auto end() const { return iterator(array_pack_ends(), sub_ranges.end(), 0); }

  auto rbegin() { return std::reverse_iterator<iterator>(end()); }
  auto rend() { return std::reverse_iterator<iterator>(begin()); }
  auto rbegin() const { return std::reverse_iterator<iterator>(end()); }
  auto rend() const { return std::reverse_iterator<iterator>(begin()); }

  // https://stackoverflow.com/questions/43277513/c-iterator-with-hasnext-and-next
  // https://stackoverflow.com/questions/7758580/writing-your-own-stl-container/7759622#7759622
  // https://softwareengineering.stackexchange.com/questions/212344/is-it-bad-practice-to-make-an-iterator-that-is-aware-of-its-own-end
  template<class Op>
  void foreach_element(Op op) {
    using iterators_type = std::tuple<typename DataArray<Types>::iterator...>;
    using value_type = std::tuple<typename DataArray<Types>::reference...>;
    iterators_type value_iters =
      std::apply([](auto&&... args) { return iterators_type(args.begin()...); }, array_pack);

    for (auto iter = sub_ranges.begin(); iter != sub_ranges.end(); ++iter) {
      for (auto entry = iter->lower; entry < iter->upper; ++entry) {
        std::apply([&](auto&&... iters) { ((iters.move_entry_to(entry)), ...); }, value_iters);
        std::apply([&](auto&&... args) { return op(*args...); }, value_iters);
      }
    }
  }
};

template<typename... Types>
class DataSubsetIterator {
public:
  using value_type = std::tuple<typename DataArray<Types>::value_type...>;
  using reference = std::tuple<typename DataArray<Types>::reference...>;
  using pointer = std::tuple<typename DataArray<Types>::pointer...>;
  using difference_type = ptrdiff_t;
  using iterator_category = std::bidirectional_iterator_tag;

  // for iterators' pack
  using size_type = size_t;
  using arraies_pointer = typename std::tuple<DataArray<Types>&...>*;
  using iterators_type = std::tuple<typename DataArray<Types>::iterator...>;

  difference_type entry_offset;
  RangeSet::iterator range_iter;
  // arraies_pointer arraies_ptr{nullptr};
  // all data array iterators pack, not always in sync with the real entry
  iterators_type iterators;

  // bool is_synced{false};
  int total_jump_count{0};

  DataSubsetIterator(const DataSubsetIterator&) = default;
  // last two params seems like 2d coord.
  DataSubsetIterator(const iterators_type& iterators, RangeSet::iterator range_iter,
                     difference_type entry_offset)
    : iterators(iterators)
    , range_iter(range_iter)
    , entry_offset(entry_offset) {}


  // auto sync_iterators() {
  //   auto e = entry();
  //   std::apply(
  //     [&](auto&&... it, auto&&... array_ptr) {

  //     },
  //     std::make_tuple(iterators, arraies_ptr));
  // }

  reference operator*() {
    // entry movement is computed only when needed
    // std::apply([e = entry()](auto&&... iters) { ((iters.move_entry_to(e)), ...); }, iterators);
    // return value_pack
    sync_iterators();
    return std::apply([&](auto&&... args) { return reference((*args)...); }, iterators);
  }

  void sync_iterators() {
    // META_TRACE("total_jump_count = {}, entry = {}", total_jump_count, entry());
    auto e = entry();
    if (total_jump_count < 0) {
      _iters_to_entry<false>(e);
    } else {
      _iters_to_entry<true>(e);
    }
    total_jump_count = 0;
    // is_synced = true;
  }

  template<auto is_forward>
  void _iters_to_entry(int e) {
    std::apply([=](auto&&... iters) { (((iters.template move_entry_to<is_forward>(e)), ...)); },
               iterators);
  }


  bool operator==(const DataSubsetIterator<Types...>&
                    other) { /*for compare ends cause they have no legal entry()*/
    return other.range_iter == range_iter
             ? (other.entry_offset == entry_offset ? true : other.entry() == entry())
             : false;
  }

  bool operator!=(const DataSubsetIterator<Types...>& other) { return !(*this == other); }
  auto operator++() { return *this += 1; }
  auto operator+=(difference_type offset) {
    total_jump_count += offset;
    // offset > 0
    while (offset > 0 && (entry_offset == -1 || entry() + offset >= range_iter->upper)) {
      // promise step_size > 0 && range_iter exists
      offset -= _to_next_first();
    }
    // iterators_move_forward();
    entry_offset += offset;
    return *this;
  }
  auto operator--() { return *this -= 1; }
  auto operator-=(difference_type offset) {
    total_jump_count -= offset;

    // offset > 0
    while (offset > 0 && (entry_offset == 0 || entry() - offset < range_iter->lower)) {
      offset += _to_prev_last();
    }

    // iterators_move_backward();
    // META_INFO("move entry_offset from {} to {}", entry_offset, entry_offset - offset);
    entry_offset -= offset;
    return *this;
  }


  // step_size could not be negative
  // need to ensure that the incoming step_size does not exceed subset.end()
  auto advance(difference_type step_size) {
    return step_size > 0 ? *this += step_size : *this -= step_size;
  }

  auto entry() const {
    return (entry_offset < 0 ? range_iter->upper : range_iter->lower) + entry_offset;
  }

private:
  inline int _to_prev_last() {
    auto offset = -(entry_offset < 0 ? range_iter->length() + entry_offset : entry_offset) - 1;

    // META_TRACE("_to_prev_last() => offset: {}", offset);
    --range_iter;
    entry_offset = -1;
    return offset;
  }

  // move entry() to next(range_iter)->lower
  // return real index offset in data array
  inline int _to_next_first() {
    auto offset = (entry_offset < 0 ? -entry_offset : range_iter->length() - entry_offset);
    ++range_iter;
    entry_offset = 0;
    return offset;
  }
};

}   // namespace MS

#endif   // //METASIM_DATA_CONTAINER_HPP
