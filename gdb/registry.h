/* Macros for general registry objects.

   Copyright (C) 2011-2020 Free Software Foundation, Inc.

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

#ifndef REGISTRY_H
#define REGISTRY_H

#include <type_traits>

/* In gdb, sometimes there is a need for one module (e.g., the Python
   Type code) to attach some data to another object (e.g., an
   objfile); but it's also desirable that this be done such that the
   base object (the objfile in this example) not need to know anything
   about the attaching module (the Python code).

   This is handled using the registry system.

   An class needing to allow this sort registration can derive from
   registry using CTRP.  For example, you would write:

   class some_container : public registry<some_container> { ... }

   A module wanting to attach data to instances of some_container
   would use the "new_key" static method to register a key.  This key
   can then be passed to the "get" and "set" methods to handle this
   module's data.

   The data in question is an uninterpreted "void *".  For a more
   convenient type-safe approach to handling data, see the
   registry_key template class below.  */

template<typename T>
class registry
{
public:

  registry ()
    : m_fields (m_registrations.size ())
  {
  }

  virtual ~registry ()
  {
    clear_registry ();
  }

  DISABLE_COPY_AND_ASSIGN (registry);

  /* Registry callbacks have this type.  */
  typedef void (*registry_data_callback) (T *, void *);

  /* The type of a key.  To ensure that this is opaque to callers, all
     the members are private.  */
  class registry_data
  {
    unsigned index;
    registry_data_callback save;
    registry_data_callback free;

    friend class registry<T>;
  };

  /* Get a new key for this particular registry.  SAVE and FREE are of
     type registry_data_callback.  When the container object is
     destroyed, first all registered SAVE functions are called.  Then
     all FREE functions are called.  Either or both may be nullptr.
     The data associated with the container object is passed to each
     function.  */
  static const registry_data *new_key (registry_data_callback save,
				       registry_data_callback free)
  {
    std::unique_ptr<registry_data> result (new registry_data);
    result->index = m_registrations.size ();
    result->save = save;
    result->free = free;
    m_registrations.emplace_back (std::move (result));
    return m_registrations.back ().get ();
  }

  /* Set the datum associated with KEY in this container.  */
  void set (const registry_data *key, void *datum)
  {
    m_fields[key->index] = datum;
  }

  /* Fetch the datum associated with KEY in this container.  If 'set'
     has not been called for this key, nullptr is returned.  */
  void *get (const registry_data *key)
  {
    return m_fields[key->index];
  }

  /* Clear all the data associated with this container.  This is
     dangerous and should not normally be done.  In the future this
     method will be made 'protected'.  */
  void clear_registry ()
  {
    T *self = dynamic_cast<T *> (this);
    /* Call all the save functions first.  */
    for (auto &datum : m_registrations)
      {
	void *elt = m_fields[datum->index];
	if (elt != nullptr && datum->save != nullptr)
	  datum->save (self, elt);
      }
    /* Next, call all the free functions.  */
    for (auto &datum : m_registrations)
      {
	void *elt = m_fields[datum->index];
	if (elt != nullptr && datum->free != nullptr)
	  datum->free (self, elt);
      }
  }

private:

  /* The data stored in this instance.  */
  std::vector<void *> m_fields;

  /* All the registrations that have been made.  We do separate
     allocations here so that the addresses are stable and can be used
     as keys.  */
  static std::vector<std::unique_ptr<registry_data>> m_registrations;
};

/* Ensure that m_registrations is emitted.  */
template<typename T> std::vector<std::unique_ptr<typename registry<T>::registry_data>>
  registry<T>::m_registrations;

/* An accessor class that is used by registry_key.

   Normally, a container class derives from registry<>.  In this case,
   the default accessor is used, as it simply returns the object.

   However, a container may sometimes need to store the registry
   elsewhere.  In this case, registry_accessor can be specialized to
   perform the needed indirection.  */

template<typename T>
struct registry_accessor
{
  /* The type of the container that derives from a registry
     specialization.  */
  typedef T data_type;
  /* The type of the corresponding registry.  */
  typedef registry<T> registry_type;

  /* Given a container of type T, return its registry.  */
  registry_type *operator() (T *obj) const
  {
    return obj;
  }
};

/* A type-safe registry key.

   A registry holds just a "void *".  This is not always convenient to
   manage, so this template class can be used instead, to provide a
   type-safe interface, that also helps manage the lifetime of the
   stored objects.

   When the container is destroyed, this key arranges to destroy the
   underlying data using Deleter.  This defaults to
   std::default_delete.  */

template<typename T, typename DATA,
	 typename Deleter = std::default_delete<DATA>>
class registry_key
{
public:

  registry_key ()
    : m_key (registry_accessor<T>::registry_type::new_key (nullptr, cleanup))
  {
  }

  DISABLE_COPY_AND_ASSIGN (registry_key);

  /* Fetch the data attached to OBJ that is associated with this key.
     If no such data has been attached, nullptr is returned.  */
  DATA *get (T *obj) const
  {
    registry_accessor<T> acc;
    typename registry_accessor<T>::registry_type *reg_obj = acc (obj);
    return (DATA *) reg_obj->get (m_key);
  }

  /* Attach DATA to OBJ, associated with this key.  Note that any
     previous data is simply dropped -- if destruction is needed,
     'clear' should be called.  */
  void set (T *obj, DATA *data) const
  {
    registry_accessor<T> acc;
    typename registry_accessor<T>::registry_type *reg_obj = acc (obj);
    reg_obj->set (m_key, data);
  }

  /* If this key uses the default deleter, then this method is
     available.  It emplaces a new instance of the associated data
     type and attaches it to OBJ using this key.  The arguments, if
     any, are forwarded to the constructor.  */
  template<typename Dummy = DATA *, typename... Args>
  typename std::enable_if<std::is_same<Deleter,
				       std::default_delete<DATA>>::value,
			  Dummy>::type
  emplace (T *obj, Args &&...args) const
  {
    DATA *result = new DATA (std::forward<Args> (args)...);
    set (obj, result);
    return result;
  }

  /* Clear the data attached to OBJ that is associated with this KEY.
     Any existing data is destroyed using the deleter, and the data is
     reset to nullptr.  */
  void clear (T *obj) const
  {
    DATA *datum = get (obj);
    if (datum != nullptr)
      {
	cleanup (obj, datum);
	set (obj, nullptr);
      }
  }

private:

  /* A helper function that is called by the registry to delete the
     contained object.  */
  static void cleanup (typename registry_accessor<T>::data_type *obj,
		       void *arg)
  {
    DATA *datum = (DATA *) arg;
    Deleter d;
    d (datum);
  }

  /* The underlying key.  */
  const typename registry_accessor<T>::registry_type::registry_data *m_key;
};

#endif /* REGISTRY_H */
