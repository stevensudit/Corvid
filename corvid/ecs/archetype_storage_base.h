// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <cassert>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "../meta/forward_like.h"
#include "entity_registry.h"

// Forward declaration for friendship; defined in "scene.h".
namespace corvid { inline namespace ecs { inline namespace scenes {
class scene_base;
}}} // namespace corvid::ecs::scenes

namespace corvid { inline namespace ecs {
inline namespace archetype_storage_bases {

// CRTP base class for `archetype_storage`, `chunked_archetype_storage`, and
// `component_storage`. Provides the registry-interaction plumbing common to
// all packed ECS storages (entity ID tracking, `entity_registry` pointer,
// `store_id`, capacity limit, and the core `remove`/`erase`/`clear`
// operations built on swap-and-pop), plus multi-component row wrappers,
// bidirectional iterators, and the `add`/`erase_if`/`remove_if` family.
//
// An archetype is a storage unit defined by a fixed set of component types,
// where each component type is stored densely in arrays and rows across those
// arrays correspond to entities. Each component type is represented as a POD
// struct, usually containing floats, IDs, or small fixed-size arrays.
//
// Required customization points in `CHILD` (may be private; befriend `base_t`
// and `typename base_t::add_guard`):
//   `template<typename... Args>
//         void do_add_components(Args&&... args);`
//   `template<typename C>
//         decltype(auto) do_get_component(this auto& self, size_type ndx);`
//   `template<size_t I>
//         decltype(auto) do_get_component_by_index(this auto& self,
//         size_type ndx);`
//   `decltype(auto) do_make_components_tuple(this auto& self, size_type ndx);`
//   `void do_swap_and_pop(size_type ndx);`
//   `void do_clear_storage();`
//   `void do_resize_storage(size_type new_size);`
//
// Template parameters:
//   CHILD   - The concrete derived class (CRTP).
//   REG     - `entity_registry` instantiation. Provides types.
//   TUPLE   - `std::tuple<Cs...>` of component types.
template<typename CHILD, typename REG, typename TUPLE>
class archetype_storage_base;

template<typename CHILD, typename REG, typename... Cs>
class archetype_storage_base<CHILD, REG, std::tuple<Cs...>> {
public:
  using derived_t = CHILD;
  using registry_t = REG;
  using id_t = registry_t::id_t;
  using handle_t = registry_t::handle_t;
  using size_type = registry_t::size_type;
  using store_id_t = registry_t::store_id_t;
  using location_t = registry_t::location_t;
  using metadata_t = registry_t::metadata_t;
  using allocator_type = registry_t::allocator_type;
  using id_allocator_t =
      std::allocator_traits<allocator_type>::template rebind_alloc<id_t>;
  using id_vector_t = std::vector<id_t, id_allocator_t>;
  using tuple_t = std::tuple<Cs...>;

  static_assert(sizeof...(Cs) > 0);

  // Check whether an entity is in this storage, by ID.
  [[nodiscard]] bool contains(id_t id) const {
    if (!registry_->is_valid(id)) return false;
    return registry_->get_location(id).store_id == store_id_;
  }

  // Check whether an entity is in this storage, by handle.
  [[nodiscard]] bool contains(handle_t handle) const {
    if (!registry_->is_valid(handle)) return false;
    return contains(handle.id());
  }

  // Return the number of entities in this storage.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(ids_.size());
  }

  // Return whether storage is empty.
  [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }

  // Expose this storage's ID.
  [[nodiscard]] store_id_t store_id() const noexcept { return store_id_; }

  // Maximum number of entities allowed in this storage.
  [[nodiscard]] size_type limit() const noexcept { return limit_; }

  // Set a new entity limit. Returns true on success, false if the current size
  // exceeds the new limit.
  [[nodiscard]] bool set_limit(size_type new_limit) {
    if (new_limit < ids_.size()) return false;
    limit_ = new_limit;
    return true;
  }

  // Remove entity by ID, moving it back to staging. Returns success flag.
  bool remove(id_t id) { return do_remove_erase(id, store_id_t{}); }

  // Remove entity by handle, moving it back to staging. Returns success flag.
  bool remove(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), store_id_t{});
  }

  // Move all entities back to staging. Entities remain valid after this call.
  void remove_all() { do_remove_erase_all(store_id_t{}); }

  // Swap-and-pop the entity out of storage and destroy it in the registry.
  // Sets `id` to `id_t::invalid` on success. Returns success flag.
  bool erase(id_t& id) {
    if (!do_remove_erase(id, store_id_t::invalid)) return false;
    id = id_t::invalid;
    return true;
  }

  // Erase by handle; validates the handle first. Resets `handle` to an
  // invalid state on success. Returns success flag.
  bool erase(handle_t& handle) {
    if (!registry_->is_valid(handle)) return false;
    if (!do_remove_erase(handle.id(), store_id_t::invalid)) return false;
    handle = handle_t{};
    return true;
  }

  // Destroy all entities in the registry and empty the storage.
  void clear() { do_remove_erase_all(store_id_t::invalid); }

  // RAII guard for `add()`: captures `size()` on construction. If `disarm()`
  // is not called before destruction, rolls `ids_` and derived storage back to
  // the saved size, providing strong exception safety.
  struct add_guard {
    explicit add_guard(derived_t& owner) noexcept
        : owner_{&owner}, saved_size_{owner.size()} {}
    add_guard(const add_guard&) = delete;
    add_guard& operator=(const add_guard&) = delete;

    // Disarm the guard (success path). Returns true for use in
    // `return guard.disarm()`.
    bool disarm() noexcept {
      saved_size_ = *id_t::invalid;
      return true;
    }

    ~add_guard() { // NOLINT(bugprone-exception-escape)
      if (saved_size_ == *id_t::invalid) return;
      owner_->ids_.resize(saved_size_);
      owner_->do_resize_storage(saved_size_);
    }

  private:
    derived_t* owner_{};
    size_type saved_size_{};
  };

  // Lightweight, non-owning handle to a single entity's row. When
  // `ACCESS=access::as_mutable`, `row_lens` (mutable);
  // `ACCESS=access::as_const`, `row_view` (read-only).
  //
  // Stores a pointer to the owning base and a flat logical index. Component
  // access is dispatched through the CRTP derived class's customization
  // points. Remains valid as long as no structural mutations
  // (add/remove/erase) occur on the owning storage.
  //
  // In terms of usage, this should not be seen as a standalone type, but
  // rather as the reference type yielded by iterators and row accessors. You
  // should not be retaining or copying these around.
  template<access ACCESS = access::as_mutable>
  class row_wrapper {
  public:
    static constexpr bool mutable_v = (ACCESS == access::as_mutable);
    using base_owner_t = std::conditional_t<mutable_v, archetype_storage_base,
        const archetype_storage_base>;
    using derived_owner_t =
        std::conditional_t<mutable_v, derived_t, const derived_t>;

    row_wrapper() = default;
    row_wrapper(const row_wrapper&) = default;
    row_wrapper(row_wrapper&&) = default;

    row_wrapper& operator=(const row_wrapper&) = default;
    row_wrapper& operator=(row_wrapper&&) = default;

    // Return the owning derived storage, preserving value category.
    [[nodiscard]] decltype(auto) get_owner(this auto&& self) noexcept {
      return forward_like<decltype(self)>(*self.owner_);
    }

    // Get the flat logical index and entity ID.
    [[nodiscard]] size_type index() const noexcept { return ndx_; }
    [[nodiscard]] id_t id() const { return owner_->ids_[ndx_]; }

    // Access component by type. Constness of the return propagates from
    // `owner_`, which is typed `const derived_t*` for `row_view` and
    // `derived_t*` for `row_lens`. `auto&&` accepts both lvalue and rvalue
    // row wrappers (e.g. temporaries returned by `operator[]`).
    template<typename C>
    [[nodiscard]] decltype(auto) component(this auto&& self) noexcept {
      return self.owner_->template do_get_component<C>(self.ndx_);
    }

    // Access component by zero-based tuple index. Same constness propagation.
    template<size_t Index>
    [[nodiscard]] decltype(auto) component(this auto&& self) noexcept {
      return self.owner_->template do_get_component_by_index<Index>(self.ndx_);
    }

    // Access all components as a tuple of references.
    [[nodiscard]] decltype(auto) components(this auto&& self) noexcept {
      return self.get_owner().do_make_components_tuple(self.ndx_);
    }

  private:
    derived_owner_t* owner_{};
    size_type ndx_{};

    explicit row_wrapper(base_owner_t& owner, size_type ndx)
        : owner_{static_cast<derived_owner_t*>(&owner)}, ndx_{ndx} {}

    friend class archetype_storage_base;
  };

  // Read-only row view.
  using row_view = row_wrapper<access::as_const>;

  // Mutable row lens.
  using row_lens = row_wrapper<access::as_mutable>;

  // Bidirectional iterator. Dereferencing yields a `row_lens` or `row_view`,
  // depending on constness. Invalidated by any structural mutation
  // (add/remove/erase).
  template<access ACCESS = access::as_mutable>
  class row_iterator {
  public:
    static constexpr bool mutable_v = (ACCESS == access::as_mutable);
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;
    using value_type = std::conditional_t<mutable_v, row_lens, row_view>;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using pointer = value_type*;
    using base_owner_t = std::conditional_t<mutable_v, archetype_storage_base,
        const archetype_storage_base>;

    row_iterator() = default;
    row_iterator(const row_iterator&) = default;
    row_iterator(row_iterator&&) = default;

    row_iterator& operator=(const row_iterator&) = default;
    row_iterator& operator=(row_iterator&&) = default;

    [[nodiscard]] reference operator*() const noexcept { return row_; }
    [[nodiscard]] pointer operator->() const noexcept { return &row_; }

    row_iterator& operator++() noexcept {
      ++row_.ndx_;
      return *this;
    }
    row_iterator operator++(int) noexcept {
      auto out = *this;
      ++*this;
      return out;
    }
    row_iterator& operator--() noexcept {
      --row_.ndx_;
      return *this;
    }
    row_iterator operator--(int) noexcept {
      auto out = *this;
      --*this;
      return out;
    }

    [[nodiscard]] friend bool
    operator==(row_iterator lhs, row_iterator rhs) noexcept {
      return lhs.row_.ndx_ == rhs.row_.ndx_;
    }
    [[nodiscard]] friend bool
    operator!=(row_iterator lhs, row_iterator rhs) noexcept {
      return !(lhs == rhs);
    }

  private:
    mutable value_type row_{};

    explicit row_iterator(base_owner_t& owner, size_type ndx)
        : row_{owner, ndx} {}

    friend class archetype_storage_base;
  };

  using iterator = row_iterator<access::as_mutable>;
  using const_iterator = row_iterator<access::as_const>;

  // Atomically create an entity in the registry and insert it into this
  // storage. Returns the new entity's handle on success, or an invalid handle
  // if the registry refused creation or the storage limit would be exceeded.
  // Components are forwarded in the same order as the `Cs...` pack. Trailing
  // components may be omitted and will be default-constructed.
  template<typename... Args>
  [[nodiscard]] handle_t add_new(const metadata_t& metadata, Args&&... args) {
    auto owner = registry_->create_owner(location_t{store_id_t{}}, metadata);
    if (!owner || !add(owner.id(), std::forward<Args>(args)...)) return {};
    return owner.release();
  }

  // Insert components for an entity already in staging (`store_id ==
  // store_id_t{}`). Returns false if the entity is not in staging, is
  // invalid, or if the limit would be exceeded. Trailing components may be
  // omitted; they are default-constructed. Passing more args than components
  // is a compile-time error.
  template<typename... Args>
  [[nodiscard]] bool add(id_t id, Args&&... args) {
    static_assert(sizeof...(Args) <= sizeof...(Cs),
        "too many arguments: cannot exceed the component count");
    const auto& loc = registry_->get_location(id);
    if (loc.store_id != store_id_t{}) return false;
    const auto ndx = size();
    if (ndx >= limit_) return false;
    add_guard guard{derived()};
    // Forward provided args; default-construct any trailing omitted
    // components.
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      auto fwd = std::forward_as_tuple(std::forward<Args>(args)...);
      // do_arg binds fwd by forwarding reference without consuming it; the
      // tuple holds references, so repeated std::move is safe (false
      // positive).
      // NOLINTNEXTLINE(bugprone-use-after-move)
      derived().do_add_components(do_arg<Is>(std::move(fwd))...);
    }(std::make_index_sequence<sizeof...(Cs)>{});
    ids_.push_back(id);
    registry_->set_location(id, {store_id_, ndx});
    return guard.disarm();
  }

  // Insert components for an entity by handle. Validates the handle before
  // delegating to `add(id_t, ...)`.
  template<typename... Args>
  [[nodiscard]] bool add(handle_t handle, Args&&... args) {
    if (!registry_->is_valid(handle)) return false;
    return add(handle.id(), std::forward<Args>(args)...);
  }

  // Erase entities for which `pred(comp, id)` returns true, where `comp` is a
  // const reference to the entity's `C` component and `id` is its ID. Uses
  // swap-and-pop; `pred` must not structurally modify the storage. All erased
  // entities are destroyed in the registry. Returns the count erased.
  // Predicate shape: `(const C& comp, id_t id) -> bool`.
  template<typename C>
  size_type erase_if_component(auto pred) {
    return do_remove_erase_if_component<C>(std::move(pred),
        store_id_t::invalid);
  }

  // Overload that selects the component by zero-based tuple index rather than
  // by type. Needed when two component types in `Cs...` are identical
  // (although this should be avoided when possible).
  // Predicate shape: `(const C& comp, id_t id) -> bool`, where `C` is
  // `std::tuple_element_t<Index, tuple_t>`.
  template<size_t Index>
  size_type erase_if_component(auto pred) {
    using C = std::tuple_element_t<Index, tuple_t>;
    return erase_if_component<C>(std::move(pred));
  }

  // Erase entities for which `pred(row)` returns true, where `row` is a
  // `row_view` giving const access to all components and the entity ID. Uses
  // swap-and-pop; `pred` must not structurally modify the storage. Returns the
  // count erased.
  // Predicate shape: `(const row_view& row) -> bool`.
  size_type erase_if(auto pred) {
    return do_remove_erase_if(std::move(pred), store_id_t::invalid);
  }

  // Move entities for which `pred(comp, id)` returns true back to staging.
  // Parallel to `erase_if_component` but keeps entities alive. Returns the
  // count moved.
  // Predicate shape: `(const C& comp, id_t id) -> bool`.
  template<typename C>
  size_type remove_if_component(auto pred) {
    return do_remove_erase_if_component<C>(std::move(pred), store_id_t{});
  }

  // Overload that selects the component by zero-based tuple index.
  // Predicate shape: `(const C& comp, id_t id) -> bool`, where `C` is
  // `std::tuple_element_t<Index, tuple_t>`.
  template<size_t Index>
  size_type remove_if_component(auto pred) {
    using C = std::tuple_element_t<Index, tuple_t>;
    return remove_if_component<C>(std::move(pred));
  }

  // Move entities for which `pred(row)` returns true back to staging. Parallel
  // to `erase_if` but keeps entities alive. Returns the count moved.
  // Predicate shape: `(const row_view& row) -> bool`.
  size_type remove_if(auto pred) {
    return do_remove_erase_if(std::move(pred), store_id_t{});
  }

  // Access the row for entity `id` as a `row_lens` (mutable) or `row_view`
  // (const). Entity must be valid and in this storage; asserts in debug.
  [[nodiscard]] row_lens operator[](id_t id) noexcept {
    assert(contains(id));
    return row_lens{*this, registry_->get_location(id).ndx};
  }
  [[nodiscard]] row_view operator[](id_t id) const noexcept {
    assert(contains(id));
    return row_view{*this, registry_->get_location(id).ndx};
  }

  // Access the row for entity `id` with bounds checking. Throws
  // `std::out_of_range` if the entity is not in this storage.
  [[nodiscard]] row_lens at(id_t id) {
    if (!contains(id)) throw std::out_of_range("entity not in this storage");
    return row_lens{*this, registry_->get_location(id).ndx};
  }
  [[nodiscard]] row_view at(id_t id) const {
    if (!contains(id)) throw std::out_of_range("entity not in this storage");
    return row_view{*this, registry_->get_location(id).ndx};
  }

  // Access the row for entity with handle and bounds checking. Throws
  // `std::invalid_argument` if the handle is invalid or stale, or if the
  // entity is not in this storage.
  [[nodiscard]] row_lens at(handle_t handle) {
    if (!contains(handle))
      throw std::invalid_argument(
          "invalid handle or entity not in this storage");
    return (*this)[handle.id()];
  }
  [[nodiscard]] row_view at(handle_t handle) const {
    if (!contains(handle))
      throw std::invalid_argument(
          "invalid handle or entity not in this storage");
    return (*this)[handle.id()];
  }

  // Bidirectional iteration over all entities in storage-sequential order.
  [[nodiscard]] iterator begin() noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() noexcept { return iterator{*this, size()}; }
  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator{*this, 0};
  }
  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator{*this, size()};
  }
  [[nodiscard]] const_iterator cbegin() const noexcept {
    return const_iterator{*this, 0};
  }
  [[nodiscard]] const_iterator cend() const noexcept {
    return const_iterator{*this, size()};
  }

  // Public deleted/defined special members.
  archetype_storage_base(const archetype_storage_base&) = delete;

  // Custom move constructor: explicitly clears `other.ids_` after stealing it.
  // Derived destructors call `clear()`, which iterates `ids_` and writes to
  // the registry. The standard only guarantees a moved-from `std::vector` is
  // "valid but unspecified" - not necessarily empty - so this explicit clear
  // makes destruction safe.
  archetype_storage_base(archetype_storage_base&& other) noexcept
      : registry_{other.registry_}, store_id_{other.store_id_},
        limit_{other.limit_}, ids_{std::move(other.ids_)} {
    other.ids_.clear();
  }

  archetype_storage_base& operator=(const archetype_storage_base&) = delete;

protected:
  // Constructors are protected; only derived classes may construct.
  // NOLINT: CRTP base constructors are protected rather than using the
  // private+friend pattern, since this base is shared by multiple derived
  // classes.
  archetype_storage_base() =
      default; // NOLINT(bugprone-crtp-constructor-accessibility)

  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
  archetype_storage_base(registry_t& registry, store_id_t store_id,
      size_type limit)
      : registry_{&registry}, store_id_{store_id}, limit_{limit},
        ids_{id_allocator_t{registry.get_allocator()}} {
    if (store_id == store_id_t::invalid || store_id == store_id_t{})
      throw std::invalid_argument("store_id must be a valid non-zero value");
  }

  archetype_storage_base& operator=(archetype_storage_base&& other) noexcept {
    if (this != &other) {
      registry_ = other.registry_;
      store_id_ = other.store_id_;
      limit_ = other.limit_;
      ids_ = std::move(other.ids_);
      other.ids_.clear();
    }
    return *this;
  }

  ~archetype_storage_base() = default;

  // CRTP helpers.
  [[nodiscard]] derived_t& derived() noexcept {
    return static_cast<derived_t&>(*this);
  }
  [[nodiscard]] const derived_t& derived() const noexcept {
    return static_cast<const derived_t&>(*this);
  }

  // Swap all base-class members with `other`. `CHILD::swap()` should call
  // this, then swap its own component storage.
  void do_swap_base(archetype_storage_base& other) noexcept {
    using std::swap;
    swap(registry_, other.registry_);
    swap(store_id_, other.store_id_);
    swap(limit_, other.limit_);
    ids_.swap(other.ids_);
  }

  // Data members shared by all derived classes.
  registry_t* registry_{nullptr};
  store_id_t store_id_{store_id_t::invalid};
  size_type limit_{*id_t::invalid};
  id_vector_t ids_{};

  // Grant `scene_base` (and through it, `scene<>`) access to `do_drop_all()`.
  friend class ::corvid::ecs::scene_base;

private:
  // Fast bulk-drop: clear component and ID vectors without touching the
  // registry. Only safe when the registry will be reset wholesale immediately
  // afterward (e.g. `scene::clear()`). Called via
  // `scene_base::storage_drop_all`.
  void do_drop_all() {
    ids_.clear();
    derived().do_clear_storage();
  }

  bool do_remove_erase(id_t id, store_id_t new_store_id) {
    if (!contains(id)) return false;
    derived().do_swap_and_pop(registry_->get_location(id).ndx);
    registry_->set_location(id, {new_store_id, *store_id_t::invalid});
    return true;
  }

  void do_remove_erase_all(store_id_t new_store_id) {
    for (const auto id : ids_) registry_->set_location(id, {new_store_id});
    do_drop_all();
  }

  // Return arg I from a `forward_as_tuple` result, or a default-constructed
  // component when I is past the end of the provided arguments.
  template<size_t I, typename ArgTuple>
  static decltype(auto) do_arg(ArgTuple&& fwd) {
    if constexpr (I < std::tuple_size_v<std::remove_cvref_t<ArgTuple>>)
      return std::get<I>(std::forward<ArgTuple>(fwd));
    else
      return std::tuple_element_t<I, tuple_t>{};
  }

  // Sweep the storage, calling `pred` on component `C` (or on the full row),
  // and either erasing or removing each entity that satisfies `pred`.

  template<typename C>
  size_type do_remove_erase_if_component(auto pred, store_id_t new_store_id) {
    static_assert(std::is_invocable_r_v<bool, decltype(pred), const C&, id_t>,
        "pred must be callable as (const C& comp, id_t id) -> bool");
    size_type cnt = 0;
    for (size_type ndx{}; ndx < ids_.size();) {
      const auto& comp = derived().template do_get_component<C>(ndx);
      if (pred(comp, ids_[ndx])) {
        const auto removed_id = ids_[ndx];
        derived().do_swap_and_pop(ndx);
        registry_->set_location(removed_id, {new_store_id});
        ++cnt;
      } else
        ++ndx;
    }
    return cnt;
  }

  size_type do_remove_erase_if(auto pred, store_id_t new_store_id) {
    static_assert(std::is_invocable_r_v<bool, decltype(pred), const row_view&>,
        "pred must be callable as (const row_view& row) -> bool");
    size_type cnt = 0;
    row_view row{*this, {}};
    for (size_type ndx{}; ndx < ids_.size();) {
      row.ndx_ = ndx;
      if (pred(row)) {
        const auto removed_id = ids_[ndx];
        derived().do_swap_and_pop(ndx);
        registry_->set_location(removed_id, {new_store_id});
        ++cnt;
      } else
        ++ndx;
    }
    return cnt;
  }
};

}}} // namespace corvid::ecs::archetype_storage_bases
