/* A hash table.

   Copyright (C) 2023 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef GDBSUPPORT_HASH_TABLE_H
#define GDBSUPPORT_HASH_TABLE_H

#include <iterator>
#include <utility>

namespace gdb {

namespace detail {
/* A helper function that finds the lowest prime greater than N.  */
extern size_t higher_prime (size_t n);
}

/* A hash table.

   Currently this is an open-addressed hash table, using Robin Hood
   probing and backward-shift deletion.  These choices are described
   in https://thenumb.at/Hashtables/

   This hash table is also slightly based on libiberty's.  In
   particular the prime sizes are taken from there.

   A hash table is parameterized by a Traits type.  This type
   must supply some static methods and types:

   . Traits::value_type
   The values stored in the hash table.  The default constructor for a
   value_type must construct an empty value.  Objects of value_type
   must be movable.

   . Traits::is_empty (const value_type &) -> bool
   Return true if the argument is empty.

   . Traits::equals (const value_type &, const T &) -> bool
   Return true if the two values are equal.  Note that the hash table
   will accept any type for some lookups and will attempt to pass this
   to Traits::equals; this can have overloads or be a template if you
   need to separate the value type from the lookup type.

   . Traits::hash (const T &) -> size_t
   Compute the hash value of the argument.

   Both 'equals' and 'hash' must accept a value_type for T.  If both
   are overloaded for other types, then operations like 'find' and
   'erase' will work with those types as well.  This enables lookups
   using just a key type and not a full object.

   Some typical implementations are provided; see the hash_table<>
   template below.

   The usual iterators are provided.  Iterators should be considered
   invalid whenever the hash table is modified.

   Unlike the libiberty hash table, no controls are provided for the
   allocation of the underlying vector or of entries.  However, it's
   perfectly ok to store unique_ptr or other smart pointers in this
   hash table, so entries can be self-managing.

   Currently this hash table is fixed at a 0.5 load factor.  This
   roughly corresponds to what libiberty does as well, though
   libiberty will also rehash when shrinking, which this table does
   not do.
*/
template<typename Traits>
class traited_hash_table
{
public:

  /* The type that is contained in this hash table.  */
  typedef typename Traits::value_type value_type;

private:

  /* The implementation of iterator types for this hash table.  This
     is a template that is instantiated for both const and non-const
     types.  */
  template<typename V>
  class iterator_templ
  {
  public:

    /* Some types required for iterators.  */
    typedef V value_type;
    typedef V *pointer;
    typedef V &reference;
    typedef std::ptrdiff_t difference_type;
    typedef std::forward_iterator_tag iterator_category;

    iterator_templ (const iterator_templ<V> &) = default;
    iterator_templ (iterator_templ<V> &&) = default;
    iterator_templ<V> &operator= (const iterator_templ<V> &) = default;
    iterator_templ<V> &operator= (iterator_templ<V> &&) = default;

    V &operator* ()
    {
      /* const_cast is used here to make sharing the implementation
	 between const and non-const simpler.  */
      return const_cast<V &> (m_data[m_i - 1]);
    }

    V *operator-> ()
    {
      /* const_cast is used here to make sharing the implementation
	 between const and non-const simpler.  */
      return const_cast<V *> (&m_data[m_i - 1]);
    }

    iterator_templ<V> &operator++ ()
    {
      /* Iteration is done in "reverse" order, to make it a little
	 simpler -- no checks of the underlying vector size are
	 needed, only comparisons against 0.  */
      --m_i;
      skip_empty ();
      return *this;
    }

    bool operator== (const iterator_templ<V> &other) const
    {
      return m_i == other.m_i && &m_data == &other.m_data;
    }

    bool operator!= (const iterator_templ<V> &other) const
    { return !(*this == other); }

  private:

    /* Only allow private construction.  */
    friend class traited_hash_table<Traits>;

    /* Create an iterator pointing at a particular slot.  The caller
       must assure that the slot is not empty; it is also ok to pass 0
       as the index.  In the non-zero case, NDX must be one plus the
       desired index.  */
    iterator_templ (size_t ndx,
		    const std::vector<typename Traits::value_type> &data)
      : m_i (ndx),
	m_data (data)
    { }

    /* Create a 'begin' iterator.  */
    explicit iterator_templ (const std::vector<typename Traits::value_type> &data)
      : m_i (data.size ()),
	m_data (data)
    {
      skip_empty ();
    }

    /* Helper method to ensure that the iterator is not pointing at an
       empty slot.  */
    void skip_empty ()
    {
      while (m_i > 0 && Traits::is_empty (m_data[m_i - 1]))
	--m_i;
    }

    /* The index into the hash table, plus one.  This is done so that
       0 can be the "end" sentinel.  */
    size_t m_i;
    /* The container data.  */
    const std::vector<typename Traits::value_type> &m_data;
  };

  /* Implementation of find.  This is written as a template so that
     any type that is supported by the traits can be used to look up
     an entry, and also to support both const and non-const
     lookups.  */
  template<typename T, typename V>
  iterator_templ<V> find_impl (const T &val, size_t hash) const
  {
    /* Maybe there are no entries.  Note that this also avoids mod by
       zero when DSIZE==0.  */
    if (m_entries == 0)
      return iterator_templ<V> (0, m_data);

    size_t dsize = m_data.size ();
    size_t ndx = hash % dsize;

    /* This will terminate because there is always at least one empty
       entry.  */
    while (true)
      {
	if (Traits::is_empty (m_data[ndx]))
	  return iterator_templ<V> (0, m_data);
	if (Traits::equals (m_data[ndx], val))
	  return iterator_templ<V> (ndx + 1, m_data);
	++ndx;
	if (ndx == dsize)
	  ndx = 0;
      }
  }

public:

  traited_hash_table () = default;

  /* Create a new hash table that will allow for SIZE elements to be
     inserted without resizing.  */
  explicit traited_hash_table (size_t size)
    : m_data (detail::higher_prime (2 * size + 1))
  { }

  /* Copying and moving.  */
  explicit traited_hash_table (const traited_hash_table<Traits> &) = default;
  explicit traited_hash_table (traited_hash_table<Traits> &&) = default;
  traited_hash_table<Traits> &operator= (const traited_hash_table<Traits> &)
    = default;
  traited_hash_table<Traits> &operator= (traited_hash_table<Traits> &&)
    = default;

  /* Iterator types.  */
  typedef iterator_templ<value_type> iterator;
  typedef iterator_templ<const value_type> const_iterator;

  /* Return a starting iterator.  */
  iterator begin ()
  { return iterator (m_data); }
  const_iterator begin () const
  { return const_iterator (m_data); }
  const_iterator cbegin () const
  { return const_iterator (m_data); }

  /* Return an end iterator.  */
  iterator end ()
  { return iterator (0, m_data); }
  const_iterator end () const
  { return const_iterator (0, m_data); }
  const_iterator cend () const
  { return const_iterator (0, m_data); }

  /* Erase an element given a lookup key and a hash.  This is a
     template so that any type supported by the traits can be
     used.  */
  template<typename T>
  void erase (const T &val, size_t hash)
  {
    /* Maybe there are no entries.  Note that this also avoids mod by
       zero when DSIZE==0.  */
    if (m_entries == 0)
      return;

    size_t dsize = m_data.size ();
    size_t ndx = hash % dsize;
    /* This will terminate because there is always at least one empty
       entry.  */
    while (true)
      {
	if (Traits::is_empty (m_data[ndx]))
	  {
	    /* Not found.  */
	    break;
	  }
	if (Traits::equals (m_data[ndx], val))
	  {
	    /* Backward-shift deletion.  The idea here is that, due to
	       Robin Hood probing, we do not need tombstones but
	       instead can simply shift entries down -- if we find an
	       element that is already at its desired location,
	       iteration stops.  */
	    --m_entries;
	    /* This will terminate because there is always at least
	       one empty entry.  */
	    while (true)
	      {
		size_t prev_ndx = ndx++;
		if (ndx == dsize)
		  ndx = 0;
		if (Traits::is_empty (m_data[ndx]))
		  {
		    /* Nothing to move.  */
		    m_data[prev_ndx] = {};
		    return;
		  }
		size_t nhash = Traits::hash (m_data[ndx]);
		if (nhash % dsize == ndx)
		  {
		    /* This element is at its best spot, so no need to
		       keep moving.  */
		    m_data[prev_ndx] = {};
		    return;
		  }
		m_data[prev_ndx] = std::move (m_data[ndx]);
	      }
	    /* Deleted the element, so we're done.  */
	    break;
	  }
	++ndx;
	if (ndx == dsize)
	  ndx = 0;
      }
  }

  /* Erase an entry.  Any type supported by the trait is accepted
     here.  */
  template<typename T>
  void erase (const T &val)
  {
    erase (val, Traits::hash (val));
  }

  /* Insert an element into the hash table.  VAL is the new element;
     it is copied.  Returns a pair whose second element is a boolean.
     This boolean is true if a new element was inserted, and false
     otherwise.  The first element of the pair is an iterator to
     either the new element, or the existing element that prevented
     insertion.  */
  std::pair<iterator, bool> insert (const value_type &val)
  {
    value_type copy = val;
    return insert (std::move (copy));
  }

  /* Insert an element into the hash table.  VAL is the new element;
     it is moved.  Returns a pair whose second element is a boolean.
     This boolean is true if a new element was inserted, and false
     otherwise.  The first element of the pair is an iterator to
     either the new element, or the existing element that prevented
     insertion.  */
  std::pair<iterator, bool> insert (value_type &&val)
  {
    /* Load factor 0.5.  The +1 here is to handle the case where the
       vector has size 0.  */
    if (m_entries * 2 + 1 > m_data.size ())
      resize ();

    size_t hash = Traits::hash (val);
    size_t dsize = m_data.size ();
    size_t cost = 0;
    size_t ndx = hash % dsize;

    /* This will terminate because there is always at least one empty
       entry.  */
    while (true)
      {
	if (Traits::is_empty (m_data[ndx]))
	  {
	    ++m_entries;
	    m_data[ndx] = std::move (val);
	    return { iterator (ndx + 1, m_data), true };
	  }
	if (Traits::equals (m_data[ndx], val))
	  return { iterator (ndx + 1, m_data), false };

	size_t nhash = Traits::hash (m_data[ndx]);
	size_t nndx = nhash % dsize;

	/* The cost is how far the entry is from its desired spot.
	   However, there is wraparound to deal with.  */
	size_t ncost;
	if (ndx >= nndx)
	  {
	    /* --- 0 ... NDX ... NNDX ... END --- */
	    ncost = ndx - nndx;
	  }
	else
	  {
	    /* --- 0 ... NNDX ... NDX ... END --- */
	    ncost = nndx + dsize - ndx;
	  }

	/* Steal from the rich.  */
	if (cost > ncost)
	  {
	    std::swap (val, m_data[ndx]);
	    cost = ncost;
	  }

	++ndx;
	if (ndx == dsize)
	  ndx = 0;
	++cost;
      }
  }

  /* Find an element.  Returns an iterator to the element, or an 'end'
     iterator if the element is not found.  */
  template<typename T>
  iterator find (const T &val, size_t hash)
  { return find_impl<T, value_type> (val, hash); }

  template<typename T>
  iterator find (const T &val)
  { return find_impl<T, value_type> (val, Traits::hash (val)); }

  template<typename T>
  const_iterator find (const T &val, size_t hash) const
  { return find_impl<T, const value_type> (val, hash); }

  template<typename T>
  const_iterator find (const T &val) const
  { return find_impl<T, const value_type> (val, Traits::hash (val)); }

  /* Return true if the hash table contains VAL, or false if not.  */
  template<typename T>
  bool contains (const T &val) const
  { return find (val) != end (); }

  /* Empty the hash table.  */
  void clear ()
  {
    m_entries = 0;
    m_data.clear ();
  }

  /* Return true if the hash table is empty, false if it has any
     entries.  */
  bool empty () const
  { return m_entries == 0; }

  /* Return the number of entries currently stored in this hash
     table.  */
  size_t size () const
  { return m_entries; }

  /* The size of the vector that underlies this hash table.  This is
     not generally useful, but is used by the self tests.  Note that
     this is not the same as the number of elements that can be
     inserted without resizing.  */
  size_t capacity () const
  { return m_data.size (); }

private:

  /* Helper method to resize the hash table.  */
  void resize ()
  {
    std::vector<value_type> saved
      (detail::higher_prime (m_data.size ()));
    std::swap (saved, m_data);
    m_entries = 0;

    for (value_type &elt : saved)
      {
	if (!Traits::is_empty (elt))
	  insert (std::move (elt));
      }
  }

  /* Number of non-empty entries in the table.  */
  size_t m_entries = 0;
  /* The underlying data.  */
  std::vector<value_type> m_data;
};


/* A typical traits implementation for a type.  This uses the standard
   hash function and the standard equality function.  */
template<typename T>
struct typical_hash_traits
{
  using value_type = T;

  template<typename U>
  static bool equals (const value_type &lhs, const U &rhs)
  { return lhs == rhs; }

  static bool is_empty (const value_type &val)
  { return ! bool (val); }

  static size_t hash (const value_type &val)
  { return std::hash<value_type> () (val); }
};

/* A hash set that stores elements of a type T.  */
template<typename T>
using hash_set = traited_hash_table<typical_hash_traits<T>>;

/* A trait to use for compatibility with the libiberty hash table.  It
   is parameterized by the hash and equality functions.  This can only
   be used for pointer types.  */
template<htab_hash Hash, htab_eq Equal, typename T>
struct libiberty_traits
{
  typedef T value_type;

  static bool equals (const value_type &lhs, const value_type &rhs)
  { return Equal (lhs, rhs); }

  static bool is_empty (const value_type &val)
  { return val == nullptr; }

  static size_t hash (const value_type &val)
  { return Hash (val); }
};

/* Traits for use in a hash map.  These use ordinary hash table traits
   for the key; the key type is taken from the traits.  The table
   itself stores key-value pairs.  */
template<typename Traits, typename Value>
struct hash_map_traits
{
  /* A convenience typedef for the key type.  */
  typedef typename Traits::value_type key_type;

  typedef std::pair<key_type, Value> value_type;

  static bool equals (const value_type &lhs, const value_type &rhs)
  { return Traits::equals (lhs.first, rhs.first); }

  static bool equals (const value_type &lhs, const key_type &rhs)
  { return Traits::equals (lhs.first, rhs); }

  static bool is_empty (const value_type &val)
  { return Traits::is_empty (val.first); }

  static size_t hash (const key_type &key)
  { return Traits::hash (key); }

  static size_t hash (const value_type &val)
  { return Traits::hash (val.first); }
};

/* A hash map.  This is parameterized by a trait type that describes
   the keys, and a value type.  The map itself is just a hash table
   whose entries are std::pair<key, value>.  */
template<typename Traits, typename Value>
class traited_hash_map
  : public traited_hash_table<hash_map_traits<Traits, Value>>
{
  using trait_type = hash_map_traits<Traits, Value>;
  using super_type = traited_hash_table<trait_type>;
  typedef typename trait_type::key_type key_type;

public:

  /* Like traited_hash_table::insert, but allows the key and value to
     be passed as-is.  */
  std::pair<typename super_type::iterator, bool> insert (const key_type &key,
							 const Value &value)
  { return super_type::insert (std::pair<key_type, Value> (key, value)); }
};

/* An easy-to-instantiate hash map that uses the "typical" hash traits
   for the key.  */
template<typename Key, typename Value>
using hash_map = traited_hash_map<typical_hash_traits<Key>, Value>;

} /* namespace gdb */

#endif /* GDBSUPPORT_HASH_TABLE_H */
