/* ELF attributes support (based on ARM EABI attributes).
   Copyright (C) 2005-2026 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* Design note regarding the merge of Object Attributes v2 during linkage

   Entry point: _bfd_elf_link_setup_object_attributes

   The linker is an "advanced" consumer of OAv2.  After parsing, it deduplicates
   them, merges them, detects any compatibility issues, and finally translates
   them to GNU properties.

   ** Overall design

   The OAv2 processing pipeline follows a map-reduce pattern.  Obviously, the
   actual processing in GNU ld is not multi-threaded, and the operations are not
   necessarily executed directly one after another.

   * Phase 1, map: successive per-file operations applied on the list of
     compatible input objects.
     1. Parsing of the OAv2 section's data (also used by objcopy).
     2. Translation of relevant GNU properties to OAv2. This is required for the
        backward-compatibility with input objects only marked using GNU
        properties.
     3. Sorting of the subsections and object attributes. Further operations
        rely on the ordering to perform some optimization in the processing of
        the data.
     4. Deduplication of subsections and object attributes, and detection of any
        conflict between duplicated subsections or tags.
     5. Translation of relevant OAv2 to GNU properties for a forward
        -compatibility with the GNU properties merge.
     6. Marking of unknown subsections to skip them during the merge (in
        phase 2), and to prune them before the output object's serialization
        (in phase 3).

   * Phase 2, reduce: OAv2 in input objects are merged together.
     1. Gathering of "frozen" values (=coming from the command-line arguments)
        into a virtual read-only list of subsections and attributes.
     2. Merging of OAv2 from an input file and the frozen input.
     3. Merging of the results of step 2 together. Since the OAv2 merge is
        commutative and associative, it can be implemented as a reduce.
        However, GNU ld implements it as an accumulate because it does not
        support multithreading.
     Notes: the two merge phases also perform a marking of unsupported / invalid
     subsections and attributes.  This marking can be used for debugging, and
     also more practically to drop unsupported optional subsections from the
     output.

   * Phase 3, finalization of the output.
     1. Pruning of the unknown / unsupported / invalid subsections and
        attributes.
     2. Serialization of OAv2 data (also used by objcopy).
     Notes:
      - There is no translation of the merged OAv2 to GNU properties at this
        stage, as the GNU properties merge process has already all the needed
        information (translated in step 5 of stage 1) to produce the GNU
        properties.
      - The GNU properties are currently required as the runtime linker does
        not understand OAv2 yet.
      - Phase 3 should also include a compatibility check between the final
        merge result of the current link unit and input shared objects.  I opted
        for postponing this compatibility check, and GNU properties merge will
        take care of it as it already does.

   The Object Ottributes merge process must handle both optional and required
   subsections.  It also treats the first merge of the frozen set specially, as
   the OAv2 list in the input BFD serves as the accumulator for subsequent
   merges.

   ** Optional subsections

   Optional subsections are processed as if merging two ordered sets — by
   iterating linearly through both, checking whether an element of a given
   ordinality is present in the opposite set, and adding it to the accumulator.
   The added diffuculty with subsections and attributes lies in the fact that
   missing elements have default values, and these must be merged with existing
   ones to produce the final value to be stored.

   ** Required subsections

   Required subsections are processed slightly differently from the optional
   subsections, as they cannot be pruned since they are mandatory, hence an
   error will be raised by the linker if it is not recognized.

   For now, the subsection for PAuth ABI is the only one use case, and no merge
   is applied on the values.  The values simply need to match.
   This implementation choice might be challenged in the future if required
   subsections can have the same diversity as optional subsections.  If the case
   arises, the refactoring to handle this new behavior should consist in adding
   a new merge policy MERGE-EQUAL, or something similar.  Some "if required"
   should be added in the optional subsections merge logic to error on any
   missing elements, or mismatch, and messages should also be rephrased to point
   out that the error is for a required subsection.

   ** Important note regarding support for testing

   In order to test this generic logic, AArch64's use cases are not offering
   enough coverage, so a "GNU testing namespace" which corresponds to the name
   of the subsection was introduced.  It follows the following pattern:
     gnu_testing_<XXXXXX>_MERGE_<POLICY>
   with:
     - <XXXXXX>: an arbitrary name for your testing subsection.
     - <POLICY>: the name of the merging policy to apply on the values in the
       subsection.  The currently supported merge policy are:
         * _MERGE_AND: bitwise AND applied on numerical values.
         * _MERGE_OR: bitwise OR applied on numerical values.
         * _MERGE_ADD: concatenates strings together with a '+' in-between.
       Note: "_MERGE_ADD" does not make really sense, and will very likely never
       be used for a real merge.  Its only purpose is to test the correct
       handling of merges with strings.
   Any subsection name matching neither names supported by the backend, nor
   following the pattern corresponding GNU testing namespace will be considered
   unknown and its status set to obj_attr_subsection_v2_unknown.  This will have
   for consequence the pruning of this subsection.

   Additionally, the first two tags in gnu_testing namespace, GNUTestTag_0 and
   GNUTestTag_1, are known, and so have a name and can be initialized to the
   default value ('0' or NULL) depending on the encoding specified on the
   subsection.  Any tags above 1 will be considered unknown, so will be default
   -initialized in the same way but its status will be set to obj_attr_v2_unknown.
   This behavior of the testing tags allows to test the pruning of unknown
   attributes.  */

#include "sysdep.h"
#include "bfd.h"
#include "doubly-linked-list.h"
#include "libiberty.h"
#include "libbfd.h"
#include "elf-bfd.h"

/* Decode the encoded version number corresponding to the Object Attribute
   version.  Return the version on success, UNSUPPORTED on failure.  */
obj_attr_version_t
_bfd_obj_attrs_version_dec (uint8_t encoded_version)
{
  if (encoded_version == 'A')
    return OBJ_ATTR_V1;
  return OBJ_ATTR_VERSION_UNSUPPORTED;
}

/* Encode the Object Attribute version into a byte.  */
uint8_t
_bfd_obj_attrs_version_enc (obj_attr_version_t version)
{
  if (version == OBJ_ATTR_V1)
    return 'A';
  abort ();
}

/* Return the number of bytes needed by I in uleb128 format.  */
static uint32_t
uleb128_size (uint32_t i)
{
  uint32_t size = 1;
  while (i >= 0x80)
    {
      i >>= 7;
      size++;
    }
  return size;
}

/* Return TRUE if the attribute has the default value (0/"").  */
static bool
is_default_attr (obj_attribute *attr)
{
  if (ATTR_TYPE_HAS_ERROR (attr->type))
    return true;
  if (ATTR_TYPE_HAS_INT_VAL (attr->type) && attr->i != 0)
    return false;
  if (ATTR_TYPE_HAS_STR_VAL (attr->type) && attr->s && *attr->s)
    return false;
  if (ATTR_TYPE_HAS_NO_DEFAULT (attr->type))
    return false;

  return true;
}

/* Return the vendor name for a given object attributes section.  */
static const char *
obj_attr_v1_vendor_name (bfd *abfd, int vendor)
{
  return (vendor == OBJ_ATTR_PROC
	  ? get_elf_backend_data (abfd)->obj_attrs_vendor
	  : "gnu");
}

/* Return the size of a single attribute.  */
static bfd_vma
obj_attr_v1_size (unsigned int tag, obj_attribute *attr)
{
  bfd_vma size;

  if (is_default_attr (attr))
    return 0;

  size = uleb128_size (tag);
  if (ATTR_TYPE_HAS_INT_VAL (attr->type))
    size += uleb128_size (attr->i);
  if (ATTR_TYPE_HAS_STR_VAL (attr->type))
    size += strlen ((char *)attr->s) + 1;
  return size;
}

/* Return the size of the object attributes section for VENDOR
   (OBJ_ATTR_PROC or OBJ_ATTR_GNU), or 0 if there are no attributes
   for that vendor to record and the vendor is OBJ_ATTR_GNU.  */
static bfd_vma
vendor_obj_attrs_v1_size (bfd *abfd, int vendor)
{
  bfd_vma size;
  obj_attribute *attr;
  obj_attribute_list *list;
  int i;
  const char *vendor_name = obj_attr_v1_vendor_name (abfd, vendor);

  if (!vendor_name)
    return 0;

  attr = elf_known_obj_attributes (abfd)[vendor];
  size = 0;
  for (i = LEAST_KNOWN_OBJ_ATTRIBUTE; i < NUM_KNOWN_OBJ_ATTRIBUTES; i++)
    size += obj_attr_v1_size (i, &attr[i]);

  for (list = elf_other_obj_attributes (abfd)[vendor];
       list;
       list = list->next)
    size += obj_attr_v1_size (list->tag, &list->attr);

  /* <size> <vendor_name> NUL 0x1 <size> */
  return (size
	  ? size + 10 + strlen (vendor_name)
	  : 0);
}

static bfd_vma
oav1_section_size (bfd *abfd)
{
  bfd_vma size = 0;
  size = vendor_obj_attrs_v1_size (abfd, OBJ_ATTR_PROC);
  size += vendor_obj_attrs_v1_size (abfd, OBJ_ATTR_GNU);
  if (size > 0)
    size += sizeof (uint8_t); /* <format-version: uint8>  */
  return size;
}

/* Return the size of a single attribute.  */
static bfd_vma
oav2_attr_size (const obj_attr_v2_t *attr, obj_attr_encoding_v2_t type)
{
  bfd_vma size = uleb128_size (attr->tag);
  switch (type)
    {
    case OA_ENC_ULEB128:
      size += uleb128_size (attr->val.uint);
      break;
    case OA_ENC_NTBS:
      size += strlen (attr->val.string) + 1; /* +1 for '\0'.  */
      break;
    default:
      abort ();
    }
  return size;
}

/* Return the size of a subsection.  */
static bfd_vma
oav2_subsection_size (const obj_attr_subsection_v2_t *subsec)
{
  bfd_vma size = sizeof (uint32_t); /* <subsection-length: uint32>  */
  size += strlen (subsec->name) + 1; /* <subsection-name: NTBS>  so +1 for '\0'.  */
  size += 2 * sizeof (uint8_t); /* <optional: uint8> <encoding: uint8>  */
  /* <attribute>*  */
  for (const obj_attr_v2_t *attr = subsec->first;
       attr != NULL;
       attr = attr->next)
    size += oav2_attr_size (attr, subsec->encoding);
  return size;
}

/* Return the size of a object attributes section.  */
static bfd_vma
oav2_section_size (bfd *abfd)
{
  const obj_attr_subsection_v2_t *subsec
    = elf_obj_attr_subsections (abfd).first;
  if (subsec == NULL)
    return 0;

  bfd_vma size = sizeof (uint8_t); /* <format-version: uint8>  */
  for (; subsec != NULL; subsec = subsec->next)
    size += oav2_subsection_size (subsec);
  return size;
}

/* Return the size of the object attributes section.  */
bfd_vma
bfd_elf_obj_attr_size (bfd *abfd)
{
  obj_attr_version_t version = elf_obj_attr_version (abfd);
  switch (version)
    {
    case OBJ_ATTR_V1:
      return oav1_section_size (abfd);
    case OBJ_ATTR_V2:
      return oav2_section_size (abfd);
    case OBJ_ATTR_VERSION_NONE:
      return 0;
    default:
      abort ();
    }
}

/* Write VAL in uleb128 format to P, returning a pointer to the
   following byte.  */
static bfd_byte *
write_uleb128 (bfd_byte *p, uint32_t val)
{
  bfd_byte c;
  do
    {
      c = val & 0x7f;
      val >>= 7;
      if (val)
	c |= 0x80;
      *(p++) = c;
    }
  while (val);
  return p;
}

/* Write attribute ATTR to butter P, and return a pointer to the following
   byte.  */
static bfd_byte *
write_obj_attr_v1 (bfd_byte *p, unsigned int tag, obj_attribute *attr)
{
  /* Suppress default entries.  */
  if (is_default_attr (attr))
    return p;

  p = write_uleb128 (p, tag);
  if (ATTR_TYPE_HAS_INT_VAL (attr->type))
    p = write_uleb128 (p, attr->i);
  if (ATTR_TYPE_HAS_STR_VAL (attr->type))
    {
      int len;

      len = strlen (attr->s) + 1;
      memcpy (p, attr->s, len);
      p += len;
    }

  return p;
}

/* Write the contents of the object attributes section (length SIZE)
   for VENDOR to CONTENTS.  */
static void
write_vendor_obj_attrs_v1 (bfd *abfd, bfd_byte *contents, bfd_vma size,
			   int vendor)
{
  bfd_byte *p;
  obj_attribute *attr;
  obj_attribute_list *list;
  int i;
  const char *vendor_name = obj_attr_v1_vendor_name (abfd, vendor);
  size_t vendor_length = strlen (vendor_name) + 1;

  p = contents;
  bfd_put_32 (abfd, size, p);
  p += 4;
  memcpy (p, vendor_name, vendor_length);
  p += vendor_length;
  *(p++) = Tag_File;
  bfd_put_32 (abfd, size - 4 - vendor_length, p);
  p += 4;

  attr = elf_known_obj_attributes (abfd)[vendor];
  for (i = LEAST_KNOWN_OBJ_ATTRIBUTE; i < NUM_KNOWN_OBJ_ATTRIBUTES; i++)
    {
      unsigned int tag = i;
      if (get_elf_backend_data (abfd)->obj_attrs_order)
	tag = get_elf_backend_data (abfd)->obj_attrs_order (i);
      p = write_obj_attr_v1 (p, tag, &attr[tag]);
    }

  for (list = elf_other_obj_attributes (abfd)[vendor];
       list;
       list = list->next)
    p = write_obj_attr_v1 (p, list->tag, &list->attr);
}

static void
oav1_write_section (bfd *abfd, bfd_byte *buffer, bfd_vma size)
{
  /* This function should only be called for object attributes version 1.  */
  BFD_ASSERT (elf_obj_attr_version (abfd) == OBJ_ATTR_V1);

  bfd_byte *p = buffer;

  const struct elf_backend_data *be = get_elf_backend_data (abfd);
  /* <format-version: uint8>  */
  *(p++) = be->obj_attrs_version_enc (elf_obj_attr_version (abfd));

  for (int vendor = OBJ_ATTR_FIRST; vendor <= OBJ_ATTR_LAST; ++vendor)
    {
      bfd_vma vendor_size = vendor_obj_attrs_v1_size (abfd, vendor);
      if (vendor_size > 0)
	write_vendor_obj_attrs_v1 (abfd, p, vendor_size, vendor);
      p += vendor_size;
    }

  /* We didn't overrun the buffer.  */
  BFD_ASSERT (p <= buffer + size);
}

static bfd_byte *
oav2_write_attr (bfd_byte *p,
		 const obj_attr_v2_t *attr,
		 obj_attr_encoding_v2_t type)
{
  p = write_uleb128 (p, attr->tag);
  switch (type)
    {
    case OA_ENC_ULEB128:
      p = write_uleb128 (p, attr->val.uint);
      break;
    case OA_ENC_NTBS:
      /* +1 for '\0'.  */
      p = (bfd_byte *) stpcpy ((char *) p, attr->val.string) + 1;
      break;
    default:
      abort ();
    }
  return p;
}

static bfd_byte *
oav2_write_subsection (bfd *abfd,
		       const obj_attr_subsection_v2_t *subsec,
		       bfd_byte *p)
{
  /* <subsection-length: uint32>  */
  bfd_vma subsec_size = oav2_subsection_size (subsec);
  bfd_put_32 (abfd, subsec_size, p);
  p += sizeof (uint32_t);

  /* <vendor-name: NTBS>  */
  size_t vendor_name_size = strlen (subsec->name) + 1; /* +1 for '\0'.  */
  memcpy (p, subsec->name, vendor_name_size);
  p += vendor_name_size;

  /* -- <vendor-data: bytes> --  */
  /* <optional: uint8>  */
  bfd_put_8 (abfd, subsec->optional, p);
  ++p;
  /* <encoding: uint8>  */
  bfd_put_8 (abfd, obj_attr_encoding_v2_to_u8 (subsec->encoding), p);
  ++p;
  /* <attribute>*  */
  for (const obj_attr_v2_t *attr = subsec->first;
       attr != NULL;
       attr = attr->next)
    p = oav2_write_attr (p, attr, subsec->encoding);
  return p;
}

static bfd_vma
oav2_write_section (bfd *abfd, bfd_byte *buffer, bfd_vma size)
{
  /* This function should only be called for object attributes version 2.  */
  BFD_ASSERT (elf_obj_attr_version (abfd) == OBJ_ATTR_V2);

  bfd_vma section_size = oav2_section_size (abfd);
  if (section_size == 0)
    return 0;

  bfd_byte *p = buffer;

  const struct elf_backend_data *be = get_elf_backend_data (abfd);
  /* <format-version: uint8>  */
  *(p++) = be->obj_attrs_version_enc (elf_obj_attr_version (abfd));

  /* [ <subsection-length: uint32> <vendor-name: NTBS> <vendor-data: bytes> ]*  */
  for (const obj_attr_subsection_v2_t *subsec
	 = elf_obj_attr_subsections (abfd).first;
       subsec != NULL;
       subsec = subsec->next)
    p = oav2_write_subsection (abfd, subsec, p);

  /* We didn't overrun the buffer.  */
  BFD_ASSERT (p <= buffer + size);
  /* We wrote as many data as it was computed by
     vendor_section_obj_attr_using_subsections_size().  */
  BFD_ASSERT (section_size == (bfd_vma) (p - buffer));

  return section_size;
}

static void
oav2_sort_subsections (obj_attr_subsection_list_t *plist)
{
  for (obj_attr_subsection_v2_t *subsec = plist->first;
       subsec != NULL;
       subsec = subsec->next)
    LINKED_LIST_MERGE_SORT (obj_attr_v2_t) (subsec, _bfd_elf_obj_attr_v2_cmp);

  LINKED_LIST_MERGE_SORT (obj_attr_subsection_v2_t)
    (plist, _bfd_elf_obj_attr_subsection_v2_cmp);
}

/* Write the contents of the object attributes section to CONTENTS.  */
void
bfd_elf_set_obj_attr_contents (bfd *abfd, bfd_byte *buffer, bfd_vma size)
{
  if (! abfd->is_linker_output
      && elf_obj_attr_version (abfd) == OBJ_ATTR_V2)
    /* Before dumping the data, sort subsections in alphabetical order, and
       attributes according to their tag in numerical order.  This is useful
       for diagnostic tools so that they dump the same output even if the
       subsections or their attributes were not declared in the same order in
       different files.
       This sorting is only performed in the case of the assembler.  In the
       case of the linker, the subsections and attributes are already sorted
       by the merge process.  */
    oav2_sort_subsections (&elf_obj_attr_subsections (abfd));

  obj_attr_version_t version = elf_obj_attr_version (abfd);
  switch (version)
    {
    case OBJ_ATTR_V1:
      oav1_write_section (abfd, buffer, size);
      break;
    case OBJ_ATTR_V2:
      oav2_write_section (abfd, buffer, size);
      break;
    default:
      abort ();
    }
}

/* The first two tags in gnu-testing namespace are known (from the perspective
   of GNU ld), and so have a name and can be initialized to the default value
   ('0' or NULL) depending on the encoding specified on the subsection.  Any
   tags above 1 will be considered unknown, so will be default initialized in
   the same way but its status will be set to obj_attr_subsection_v2_unknown.
   If the tag is unknown, ld can drop it if it is inside an optional subsection,
   whereas ld will raise an error in a required subsection.
   Note: the array below has to be sorted by the tag's integer value.  */
static const obj_attr_info_t known_attrs_gnu_testing[] =
{
  { .tag = {"GNUTestTag_0", 0} },
  { .tag = {"GNUTestTag_1", 1} },
};

/* List of known GNU subsections.
   Note: this array has to be sorted using the same criteria as in
   _bfd_elf_obj_attr_subsection_v2_cmp().  */
static const known_subsection_v2_t obj_attr_v2_known_gnu_subsections[] =
{
  {
    /* Note: the currently set values for the subsection name, its optionality,
       and encoding are irrelevant for a testing subsection.  These values are
       unused.  This entry is only a placeholder for list of known GNU testing
       tags.  */
    .subsec_name = NULL,
    .known_attrs = known_attrs_gnu_testing,
    .optional = true,
    .encoding = OA_ENC_ULEB128,
    .len = ARRAY_SIZE (known_attrs_gnu_testing),
  },
  /* Note for the future: GNU subsections can be added here below.  */
};

/* Return True if the given subsection name is part of the reserved testing
   namespace, i.e. SUBSEC_NAME begins with "gnu-testing".  */
static bool
gnu_testing_namespace (const char *subsec_name)
{
  return strncmp ("gnu_testing_", subsec_name, 12) == 0;
}

/* Identify the scope of a subsection from its name.
   Note: the code below needs to be kept in sync with the code of
   elf_parse_attrs_subsection_v2() in binutils/readelf.c.  */
obj_attr_subsection_scope_v2_t
bfd_elf_obj_attr_subsection_v2_scope (const bfd *abfd, const char *subsec_name)
{
  const char *vendor_name = get_elf_backend_data (abfd)->obj_attrs_vendor;
  obj_attr_subsection_scope_v2_t scope = OA_SUBSEC_PRIVATE;
  size_t vendor_name_len = strlen (vendor_name);
  if (strncmp (subsec_name, vendor_name, vendor_name_len) == 0
      && subsec_name[vendor_name_len] == '_')
    scope = OA_SUBSEC_PUBLIC;
  return scope;
}

/* Search for a subsection matching NAME in the list of subsections known from
   bfd (generic or backend-specific).  Return the subsection information if it
   is found, or NULL otherwise.  */
const known_subsection_v2_t *
bfd_obj_attr_v2_identify_subsection (const struct elf_backend_data *bed,
				     const char *name)
{
  /* Check known backend subsections.  */
  const known_subsection_v2_t *known_subsections
    = bed->obj_attr_v2_known_subsections;
  const size_t known_subsections_size = bed->obj_attr_v2_known_subsections_size;

  for (unsigned i = 0; i < known_subsections_size; ++i)
    {
      int cmp = strcmp (known_subsections[i].subsec_name, name);
      if (cmp == 0)
	return &known_subsections[i];
      else if (cmp > 0)
	break;
    }

  /* Check known GNU subsections.  */
  /* Note for the future: search known GNU subsections here.  Don't forget to
     skip the first entry (placeholder for GNU testing subsection).  */

  /* Check whether this subsection is a GNU testing subsection.  */
  if (gnu_testing_namespace (name))
    return &obj_attr_v2_known_gnu_subsections[0];

  return NULL;
}

/* Search for the attribute information associated to TAG in the list of known
   tags registered in the known subsection SUBSEC.  Return the tag information
   if it is found, NULL otherwise.  */
static const obj_attr_info_t *
identify_tag (const known_subsection_v2_t *subsec, obj_attr_tag_t tag)
{
  for (unsigned i = 0; i < subsec->len; ++i)
    {
      const obj_attr_info_t *known_attr = &subsec->known_attrs[i];
      if (known_attr->tag.value == tag)
	return known_attr;
      else if (known_attr->tag.value > tag)
	break;
    }
  return NULL;
}

/* Return the attribute information associated to the pair SUBSEC, TAG if it
   exists, NULL otherwise.  */
const obj_attr_info_t *
_bfd_obj_attr_v2_find_known_by_tag (const struct elf_backend_data *bed,
				    const char *subsec_name,
				    obj_attr_tag_t tag)
{
  const known_subsection_v2_t *subsec_info
    = bfd_obj_attr_v2_identify_subsection (bed, subsec_name);
  if (subsec_info != NULL)
    return identify_tag (subsec_info, tag);
  return NULL;
}

/* To-string function for the pair <SUBSEC, TAG>.
   Returns the attribute information associated to TAG if it is found,
   or "Tag_unknown_<N>" otherwise.  */
const char *
_bfd_obj_attr_v2_tag_to_string (const struct elf_backend_data *bed,
				const char *subsec_name,
				obj_attr_tag_t tag)
{
  const obj_attr_info_t *attr_info
    = _bfd_obj_attr_v2_find_known_by_tag (bed, subsec_name, tag);
  if (attr_info != NULL)
    return xstrdup (attr_info->tag.name);
  return xasprintf ("Tag_unknown_%lu", tag);
}

/* To-string function for the subsection parameter "comprehension".  */
const char *
bfd_oav2_comprehension_to_string (bool comprehension)
{
  return comprehension ? "optional" : "required";
}

/* To-string function for the subsection parameter "encoding".  */
const char *
bfd_oav2_encoding_to_string (obj_attr_encoding_v2_t encoding)
{
  return (encoding == OA_ENC_ULEB128) ? "ULEB128" : "NTBS";
}

/* Return True if the given BFD is an ELF object with the target backend
   machine code, non-dynamic (i.e. not a shared library), non-executable, not a
   plugin or created by the linker.  False otherwise.  */
static bool
oav2_relevant_elf_object (const struct bfd_link_info *info,
			  const bfd *const abfd)
{
  const struct elf_backend_data *output_bed
    = get_elf_backend_data (info->output_bfd);
  unsigned int elfclass = output_bed->s->elfclass;
  int elf_machine_code = output_bed->elf_machine_code;
  return ((abfd->flags & (DYNAMIC | EXEC_P | BFD_PLUGIN | BFD_LINKER_CREATED)) == 0
	  && bfd_get_flavour (abfd) == bfd_target_elf_flavour
	  && elf_machine_code == get_elf_backend_data (abfd)->elf_machine_code
	  && elfclass == get_elf_backend_data (abfd)->s->elfclass);
}

/* Structure storing the result of a search in the list of input BFDs.  */
typedef struct
{
  /* A pointer to a BFD.  This BFD can either point to a file having
     object attributes, or a candidate file which does not have any.  */
  bfd *pbfd;

  /* A boolean indicating whether the file actually contains object
     attributes.  */
  bool has_object_attributes;

  /* A pointer to the section containing the object attributes, if any
     were found.  */
  asection *sec;
} bfd_search_result_t;

/* Search for the first input object file containing object attributes.
   If no such object is found, the result structure's PBFD points to the
   last object file that could have contained object attributes.  The
   result structure's HAS_OBJECT_ATTRIBUTES allows to distinguish the
   cases when PBFD contains or does not contain object attributes.  If no
   candidate file is found, PBFD will stay NULL.  */
static bfd_search_result_t
bfd_linear_find_first_with_obj_attrs (const struct bfd_link_info *info)
{
  (void) info;
  /* TO IMPLEMENT */
  bfd_search_result_t res = { .has_object_attributes = false };
  return res;
}

/* Create a object attributes section for the given bfd input.  */
static asection *
create_object_attributes_section (struct bfd_link_info *info,
				  bfd *abfd)
{
  (void) info;
  (void) abfd;
  /* TO IMPLEMENT */
  return NULL;
}

/* Translate GNU properties that have object attributes v2 equivalents.  */
static void
oav2_translate_gnu_props_to_obj_attrs (const bfd *abfd)
{
  (void) abfd;
  /* TO IMPLEMENT */
}

/* Translate object attributes v2 that have GNU properties equivalents.  */
static void
oav2_translate_obj_attrs_to_gnu_props (bfd *abfd)
{
  (void) abfd;
  /* TO IMPLEMENT */
}

/* Merge duplicated subsections and object attributes inside a same object
   file.  After a call to this function, the subsections and object attributes
   are sorted.
   Note: this function allows to handle in a best effort exotic objects produced
   by a non-GNU assembler.  Duplicated subsections could come from the same
   section, or different ones.  Indeed, the deserializer deserializes the
   content of a section if its type matches the object attributes type specified
   by the backend, regardless of the section name.  The behavior for such cases
   is not specified by the Object Attributes specification, and are a question
   of implementation.  Non-GNU linkers might have a different behavior with such
   exotic objects. */
static bool
oav2_file_scope_merge_subsections (const bfd *abfd)
{
  (void) abfd;
  /* TO IMPLEMENT */
  return false;
}

/* If an Object Attribute subsection inside ABFD cannot be identified neither
   as a GNU subsection or a backend-specific one, set the status of this
   subsection to UNKNOWN.  The unknown subsection will be skipped during the
   merge process, and will be pruned from the output.  */
static void
oav2_subsections_mark_unknown (const bfd *abfd)
{
  (void) abfd;
  /* TO IMPLEMENT */
}

/* Merge object attributes from FROZEN into the object file ABFD.
   Note: this function is called only once before starting the merge process
   between the object files.  ABFD corresponds to the future REF_BFD, and is
   used to store the result of the merge.  ABFD is also an input file, so any
   mismatch against FROZEN should be raised before the values of ABFD be
   modified.  */
static bool
oav2_subsections_merge_frozen (const struct bfd_link_info *info,
			       const bfd *abfd,
			       const obj_attr_subsection_list_t *frozen_cfg)
{
  (void) info;
  (void) abfd;
  (void) frozen_cfg;
  /* TO IMPLEMENT */
  return false;
}

/* Merge object attributes from object file ABFD and FROZEN_CFG into REF_BFD.  */
static bool
oav2_subsections_merge (const struct bfd_link_info *info,
			const bfd *ref_bfd, bfd *abfd,
			const obj_attr_subsection_list_t *frozen_cfg)
{
  (void) info;
  (void) ref_bfd;
  (void) abfd;
  (void) frozen_cfg;
  /* TO IMPLEMENT */
  return false;
}

/* Wrapper for the high-level logic of merging a single file.
   It handles both of the following cases:
   - ABFD (future REF_BFD) merged against FROZEN.
   - ABFD (input) and FROZEN_CFG merged into REF_BFD.
   If has_obj_attrs_after_translation is non-NULL, the caller is responsible for
   the distinction between the cases where attributes are already present, or
   where attributes were added as a result of a translation of GNU properties.
   The first step translates existing GNU properties to object attributes. Next,
   any duplicates entries in the input are merged, and the resulting object
   attributes are written back into GNU properties so that the GNU properties
   merge process can correctly diagnose potential issues. Before merging,
   unknown subsections and attributes are marked so they can be skipped during
   processing.
   Return True on success, False on failure.  */
static bool
oav2_merge_one (const struct bfd_link_info *info,
		bfd *ref_bfd, bfd *abfd,
		const obj_attr_subsection_list_t *frozen_cfg,
		bool *has_obj_attrs_after_translation)
{
  /* ABFD is an input file that may contain GNU properties, object
     attributes, or both.  Before merging object attributes, we must first
     translate any GNU properties into their equivalent object attributes
     (if such equivalents exist) since they may not already be present.  */
  oav2_translate_gnu_props_to_obj_attrs (abfd);
  if (has_obj_attrs_after_translation
      && elf_obj_attr_subsections (abfd).size > 0)
    *has_obj_attrs_after_translation = true;

  /* Merge duplicates subsections and attributes.  */
  if (! oav2_file_scope_merge_subsections (abfd))
    return false;

  /* ABFD is an input file that may contain GNU properties, object
     attributes, or both.  Before merging GNU properties, we must first
     translate any object attributes into their equivalent GNU properties
     (if such equivalents exist) since they may not already be present.  */
  oav2_translate_obj_attrs_to_gnu_props (abfd);

  /* Note: object attributes are always merged before GNU properties.
     Ideally, there would be a single internal representation, with an
     abstraction level flexible enough to capture both GNU properties and
     object attributes without loss.  In such a design, merge order would
     be irrelevant, and translation would occur only at the I/O boundaries
     during deserialization of GNU properties and object attributes.  */

  /* Mark unknown subsections and attributes to skip them during
     the merge.  */
  oav2_subsections_mark_unknown (abfd);

  if (ref_bfd == NULL)
    /* When REF_BFD is null, it means that ABFD is the future REF_BFD, and
       will be accumulating the merge result.
       It means that we will lose information from ABFD beyond this stage,
       so we need to emit warnings / errors (if any) when merging ABFD
       against FROZEN.  */
    return oav2_subsections_merge_frozen (info, abfd, frozen_cfg);

  /* Common merge case.  */
  return oav2_subsections_merge (info, ref_bfd, abfd, frozen_cfg);
}

/* Merge all the object attributes in INPUT_BFDS (REF_BFD excluded) into
   REF_BFD.  Return True on success, False otherwise.  */
static bool
oav2_merge_all (const struct bfd_link_info *info,
		bfd *ref_bfd, bfd *input_bfds,
		obj_attr_subsection_list_t *frozen_cfg)
{
  bool success = true;
  for (bfd *abfd = input_bfds; abfd != NULL; abfd = abfd->link.next)
    {
      if (abfd != ref_bfd && oav2_relevant_elf_object (info, abfd))
	success &= oav2_merge_one (info, ref_bfd, abfd, frozen_cfg, NULL);
    }
  return success;
}

/* Prune any subsection or attribute with a status different from OK.  */
static bfd *
oav2_prune_nok_attrs (const struct bfd_link_info *info, bfd *abfd)
{
  (void) info;
  (void) abfd;
  /* TO IMPLEMENT */
  return NULL;
}

/* Set up object attributes coming from configuration, and merge them with the
   ones from the input object files.  Return a pointer to the input object file
   containing the merge result on success, NULL otherwise.  */
bfd *
_bfd_elf_link_setup_object_attributes (struct bfd_link_info *info)
{
  obj_attr_subsection_list_t *frozen_cfg
    = &elf_obj_attr_subsections (info->output_bfd);

  bfd_search_result_t res
    = bfd_linear_find_first_with_obj_attrs (info);

  /* If res.pbfd is NULL, it means that it didn't find any ELF object files.  */
  if (res.pbfd == NULL)
    return NULL;

  /* Sort the frozen subsections and attributes in case that they were not
     inserted in the correct order.  */
  oav2_sort_subsections (frozen_cfg);

  /* Merge object attributes sections.  */
  info->callbacks->minfo ("\n");
  info->callbacks->minfo (_("Merging object attributes\n"));
  info->callbacks->minfo ("\n");

  bool success = oav2_merge_one (info, NULL, res.pbfd, frozen_cfg,
				 &res.has_object_attributes);

  /* No frozen object attributes and no object file, so nothing to do.  */
  if (!res.has_object_attributes && frozen_cfg->size == 0)
    return NULL;
  /* If frozen object attributes were set by some command-line options, we still
     need to emit warnings / errors if incompatibilities exist.  */

  /* Set the object attribute version for the output object to the recommended
     value by the backend.  */
  elf_obj_attr_version (info->output_bfd)
    = get_elf_backend_data (info->output_bfd)->default_obj_attr_version;

  if (res.sec == NULL)
    {
      /* This input object has no object attribute section matching the name and
	 type specified by the backend, i.e. elf_backend_obj_attrs_section and
	 elf_backend_obj_attrs_section_type.
	 One of the two following cases is possible:
	 1. No object attribute were found in this file, so the object attribute
	    version was never set by the deserializer.
	 2. The deserializer might have found attributes in another section with
	    the correct type but the wrong name.  The object attribute version
	    should have been set correctly in this case.
	 Whatever of those two cases, we set the object attribute version to the
	 backend's recommended value, and create a new section with the expected
	 name and type.  */
      elf_obj_attr_version (res.pbfd)
	= get_elf_backend_data (res.pbfd)->default_obj_attr_version;
      res.sec = create_object_attributes_section (info, res.pbfd);
    }

  /* Merge all the input object files againt res.pbfd and frozen_cfg, and
     accumulate the merge result in res.pbfd.  */
  success &= oav2_merge_all (info, res.pbfd, info->input_bfds, frozen_cfg);
  if (! success)
    return NULL;

  /* Prune all subsections and attributes with a status different from OK.  */
  if (! oav2_prune_nok_attrs (info, res.pbfd))
    return NULL;
  BFD_ASSERT (elf_obj_attr_subsections (res.pbfd).size > 0);


  /* Shallow-copy the object attributes into output_bfd.  */
  elf_obj_attr_subsections (info->output_bfd)
    = elf_obj_attr_subsections (res.pbfd);

  /* Note: the object attributes section in the output object is copied from
     the input object which was used for the merge (res.pbfd).  No need to
     create it here.  However, so that the section is copied to the output
     object, the size must be different from 0.  For now, we will set this
     size to 1.  The real size will be set later.  */
  res.sec->size = 1;

  return res.pbfd;
}

/* Allocate/find an object attribute.  */
obj_attribute *
bfd_elf_new_obj_attr (bfd *abfd, obj_attr_vendor_t vendor, obj_attr_tag_t tag)
{
  obj_attribute *attr;
  obj_attribute_list *list;
  obj_attribute_list *p;
  obj_attribute_list **lastp;


  if (tag < NUM_KNOWN_OBJ_ATTRIBUTES)
    {
      /* Known tags are preallocated.  */
      attr = &elf_known_obj_attributes (abfd)[vendor][tag];
    }
  else
    {
      /* Create a new tag.  */
      list = (obj_attribute_list *)
	bfd_alloc (abfd, sizeof (obj_attribute_list));
      if (list == NULL)
	return NULL;
      memset (list, 0, sizeof (obj_attribute_list));
      list->tag = tag;
      /* Keep the tag list in order.  */
      lastp = &elf_other_obj_attributes (abfd)[vendor];
      for (p = *lastp; p; p = p->next)
	{
	  if (tag < p->tag)
	    break;
	  lastp = &p->next;
	}
      list->next = *lastp;
      *lastp = list;
      attr = &list->attr;
    }

  return attr;
}

/* Return the value of an integer object attribute.  */
int
bfd_elf_get_obj_attr_int (bfd *abfd,
			  obj_attr_vendor_t vendor,
			  obj_attr_tag_t tag)
{
  obj_attribute_list *p;

  if (tag < NUM_KNOWN_OBJ_ATTRIBUTES)
    {
      /* Known tags are preallocated.  */
      return elf_known_obj_attributes (abfd)[vendor][tag].i;
    }
  else
    {
      for (p = elf_other_obj_attributes (abfd)[vendor];
	   p;
	   p = p->next)
	{
	  if (tag == p->tag)
	    return p->attr.i;
	  if (tag < p->tag)
	    break;
	}
      return 0;
    }
}

/* Add an integer object attribute.  */
obj_attribute *
bfd_elf_add_obj_attr_int (bfd *abfd,
			  obj_attr_vendor_t vendor,
			  obj_attr_tag_t tag,
			  unsigned int value)
{
  obj_attribute *attr;

  attr = bfd_elf_new_obj_attr (abfd, vendor, tag);
  if (attr != NULL)
    {
      attr->type = bfd_elf_obj_attrs_arg_type (abfd, vendor, tag);
      attr->i = value;
    }
  return attr;
}

/* Duplicate an object attribute string value.  */
static char *
elf_attr_strdup (bfd *abfd, const char *s, const char *end)
{
  char *p;
  size_t len;

  if (end)
    len = strnlen (s, end - s);
  else
    len = strlen (s);

  p = (char *) bfd_alloc (abfd, len + 1);
  if (p != NULL)
    {
      memcpy (p, s, len);
      p[len] = 0;
    }
  return p;
}

char *
_bfd_elf_attr_strdup (bfd *abfd, const char *s)
{
  return elf_attr_strdup (abfd, s, NULL);
}

/* Add a string object attribute.  */
static obj_attribute *
elf_add_obj_attr_string (bfd *abfd, obj_attr_vendor_t vendor, obj_attr_tag_t tag,
			 const char *s, const char *end)
{
  obj_attribute *attr;

  attr = bfd_elf_new_obj_attr (abfd, vendor, tag);
  if (attr != NULL)
    {
      attr->type = bfd_elf_obj_attrs_arg_type (abfd, vendor, tag);
      attr->s = elf_attr_strdup (abfd, s, end);
      if (attr->s == NULL)
	return NULL;
    }
  return attr;
}

obj_attribute *
bfd_elf_add_obj_attr_string (bfd *abfd,
			     obj_attr_vendor_t vendor,
			     obj_attr_tag_t tag,
			     const char *s)
{
  return elf_add_obj_attr_string (abfd, vendor, tag, s, NULL);
}

/* Add a int+string object attribute.  */
static obj_attribute *
elf_add_obj_attr_int_string (bfd *abfd,
			     obj_attr_vendor_t vendor,
			     obj_attr_tag_t tag,
			     unsigned int i,
			     const char *s,
			     const char *end)
{
  obj_attribute *attr;

  attr = bfd_elf_new_obj_attr (abfd, vendor, tag);
  if (attr != NULL)
    {
      attr->type = bfd_elf_obj_attrs_arg_type (abfd, vendor, tag);
      attr->i = i;
      attr->s = elf_attr_strdup (abfd, s, end);
      if (attr->s == NULL)
	return NULL;
    }
  return attr;
}

obj_attribute *
bfd_elf_add_obj_attr_int_string (bfd *abfd,
				 obj_attr_vendor_t vendor,
				 obj_attr_tag_t tag,
				 unsigned int i,
				 const char *s)
{
  return elf_add_obj_attr_int_string (abfd, vendor, tag, i, s, NULL);
}

/* Copy object attributes v1 from IBFD to OBFD.  */
static void
oav1_copy_attributes (bfd *ibfd, bfd *obfd)
{
  obj_attribute *in_attr;
  obj_attribute *out_attr;
  obj_attribute_list *list;
  int i;
  obj_attr_vendor_t vendor;

  for (vendor = OBJ_ATTR_FIRST; vendor <= OBJ_ATTR_LAST; vendor++)
    {
      in_attr
	= &elf_known_obj_attributes (ibfd)[vendor][LEAST_KNOWN_OBJ_ATTRIBUTE];
      out_attr
	= &elf_known_obj_attributes (obfd)[vendor][LEAST_KNOWN_OBJ_ATTRIBUTE];
      for (i = LEAST_KNOWN_OBJ_ATTRIBUTE; i < NUM_KNOWN_OBJ_ATTRIBUTES; i++)
	{
	  out_attr->type = in_attr->type;
	  out_attr->i = in_attr->i;
	  if (in_attr->s && *in_attr->s)
	    {
	      out_attr->s = _bfd_elf_attr_strdup (obfd, in_attr->s);
	      if (out_attr->s == NULL)
		bfd_perror (_("error adding attribute"));
	    }
	  in_attr++;
	  out_attr++;
	}

      for (list = elf_other_obj_attributes (ibfd)[vendor];
	   list;
	   list = list->next)
	{
	  bool ok = false;
	  in_attr = &list->attr;
	  switch (in_attr->type & (ATTR_TYPE_FLAG_INT_VAL | ATTR_TYPE_FLAG_STR_VAL))
	    {
	    case ATTR_TYPE_FLAG_INT_VAL:
	      ok = bfd_elf_add_obj_attr_int (obfd, vendor,
					     list->tag, in_attr->i);
	      break;
	    case ATTR_TYPE_FLAG_STR_VAL:
	      ok = bfd_elf_add_obj_attr_string (obfd, vendor, list->tag,
						in_attr->s);
	      break;
	    case ATTR_TYPE_FLAG_INT_VAL | ATTR_TYPE_FLAG_STR_VAL:
	      ok = bfd_elf_add_obj_attr_int_string (obfd, vendor, list->tag,
						    in_attr->i, in_attr->s);
	      break;
	    default:
	      abort ();
	    }
	  if (!ok)
	    bfd_perror (_("error adding attribute"));
	}
    }
}

static obj_attr_subsection_v2_t *
oav2_obj_attr_subsection_v2_copy (const obj_attr_subsection_v2_t *);

/* Copy object attributes v2 from IBFD to OBFD.  */
static void
oav2_copy_attributes (bfd *ibfd, bfd *obfd)
{
  const obj_attr_subsection_list_t *in_attr_subsecs
    = &elf_obj_attr_subsections (ibfd);
  obj_attr_subsection_list_t *out_attr_subsecs
    = &elf_obj_attr_subsections (obfd);

  for (const obj_attr_subsection_v2_t *isubsec = in_attr_subsecs->first;
       isubsec != NULL;
       isubsec = isubsec->next)
    {
      obj_attr_subsection_v2_t *osubsec
	= oav2_obj_attr_subsection_v2_copy (isubsec);
      LINKED_LIST_APPEND (obj_attr_subsection_v2_t) (out_attr_subsecs, osubsec);
    }
}

/* Copy the object attributes from IBFD to OBFD.  */
void
_bfd_elf_copy_obj_attributes (bfd *ibfd, bfd *obfd)
{
  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return;

  obj_attr_version_t version = elf_obj_attr_version (ibfd);
  elf_obj_attr_version (obfd) = version;

  switch (version)
    {
    case OBJ_ATTR_VERSION_NONE:
      break;
    case OBJ_ATTR_V1:
      oav1_copy_attributes (ibfd, obfd);
      break;
    case OBJ_ATTR_V2:
      oav2_copy_attributes (ibfd, obfd);
      break;
    default:
      abort ();
    }
}

/* Determine whether a GNU object attribute tag takes an integer, a
   string or both.  */
static int
gnu_obj_attrs_arg_type (obj_attr_tag_t tag)
{
  /* Except for Tag_compatibility, for GNU attributes we follow the
     same rule ARM ones > 32 follow: odd-numbered tags take strings
     and even-numbered tags take integers.  In addition, tag & 2 is
     nonzero for architecture-independent tags and zero for
     architecture-dependent ones.  */
  if (tag == Tag_compatibility)
    return 3;
  else
    return (tag & 1) != 0 ? 2 : 1;
}

/* Determine what arguments an attribute tag takes.  */
int
bfd_elf_obj_attrs_arg_type (bfd *abfd,
			    obj_attr_vendor_t vendor,
			    obj_attr_tag_t tag)
{
  /* This function should only be called for object attributes version 1.  */
  BFD_ASSERT (elf_obj_attr_version (abfd) == OBJ_ATTR_V1);
  switch (vendor)
    {
    case OBJ_ATTR_PROC:
      return get_elf_backend_data (abfd)->obj_attrs_arg_type (tag);
      break;
    case OBJ_ATTR_GNU:
      return gnu_obj_attrs_arg_type (tag);
      break;
    default:
      abort ();
    }
}

static void
oav1_parse_section (bfd *abfd, bfd_byte *p, bfd_byte *p_end)
{
  const char *std_sec = get_elf_backend_data (abfd)->obj_attrs_vendor;

  while (p_end - p >= 4)
    {
      size_t len = p_end - p;
      size_t namelen;
      size_t section_len;
      int vendor;

      section_len = bfd_get_32 (abfd, p);
      p += 4;
      if (section_len == 0)
	break;
      if (section_len > len)
	section_len = len;
      if (section_len <= 4)
	{
	  _bfd_error_handler
	    (_("%pB: error: attribute section length too small: %ld"),
	     abfd, (long) section_len);
	  break;
	}
      section_len -= 4;
      namelen = strnlen ((char *) p, section_len) + 1;
      if (namelen >= section_len)
	break;
      if (std_sec && strcmp ((char *) p, std_sec) == 0)
	vendor = OBJ_ATTR_PROC;
      else if (strcmp ((char *) p, "gnu") == 0)
	vendor = OBJ_ATTR_GNU;
      else
	{
	  /* Other vendor section.  Ignore it.  */
	  p += section_len;
	  continue;
	}

      p += namelen;
      section_len -= namelen;
      while (section_len > 0)
	{
	  unsigned int tag;
	  unsigned int val;
	  size_t subsection_len;
	  bfd_byte *end, *orig_p;

	  orig_p = p;
	  tag = _bfd_safe_read_leb128 (abfd, &p, false, p_end);
	  if (p_end - p >= 4)
	    {
	      subsection_len = bfd_get_32 (abfd, p);
	      p += 4;
	    }
	  else
	    {
	      p = p_end;
	      break;
	    }
	  if (subsection_len > section_len)
	    subsection_len = section_len;
	  section_len -= subsection_len;
	  end = orig_p + subsection_len;
	  if (end < p)
	    break;
	  switch (tag)
	    {
	    case Tag_File:
	      while (p < end)
		{
		  int type;
		  bool ok = false;

		  tag = _bfd_safe_read_leb128 (abfd, &p, false, end);
		  type = bfd_elf_obj_attrs_arg_type (abfd, vendor, tag);
		  switch (type & (ATTR_TYPE_FLAG_INT_VAL | ATTR_TYPE_FLAG_STR_VAL))
		    {
		    case ATTR_TYPE_FLAG_INT_VAL | ATTR_TYPE_FLAG_STR_VAL:
		      val = _bfd_safe_read_leb128 (abfd, &p, false, end);
		      ok = elf_add_obj_attr_int_string (abfd, vendor, tag,
							val, (char *) p,
							(char *) end);
		      p += strnlen ((char *) p, end - p);
		      if (p < end)
			p++;
		      break;
		    case ATTR_TYPE_FLAG_STR_VAL:
		      ok = elf_add_obj_attr_string (abfd, vendor, tag,
						    (char *) p,
						    (char *) end);
		      p += strnlen ((char *) p, end - p);
		      if (p < end)
			p++;
		      break;
		    case ATTR_TYPE_FLAG_INT_VAL:
		      val = _bfd_safe_read_leb128 (abfd, &p, false, end);
		      ok = bfd_elf_add_obj_attr_int (abfd, vendor, tag, val);
		      break;
		    default:
		      abort ();
		    }
		  if (!ok)
		    bfd_perror (_("error adding attribute"));
		}
	      break;
	    case Tag_Section:
	    case Tag_Symbol:
	      /* Don't have anywhere convenient to attach these.
		 Fall through for now.  */
	    default:
	      /* Ignore things we don't know about.  */
	      p = end;
	      break;
	    }
	}
    }
}

#define READ_ULEB128(abfd, var, cursor, end, total_read)		\
  do									\
    {									\
      bfd_byte *begin = (cursor);					\
      (var) = _bfd_safe_read_leb128 (abfd, &(cursor), false, end);	\
      (total_read) += (cursor) - begin;					\
    }									\
  while (0)

static int
read_ntbs (bfd *abfd,
	   const bfd_byte *cursor,
	   const bfd_byte *end,
	   char **s)
{
  *s = NULL;
  const size_t MAX_STR_LEN = end - cursor; /* Including \0.  */
  const size_t s_len = strnlen ((const char *) cursor, MAX_STR_LEN);
  if (s_len == MAX_STR_LEN)
    {
      bfd_set_error (bfd_error_malformed_archive);
      _bfd_error_handler (_("%pB: error: NTBS value seems corrupted "
			    "(missing '\\0')"),
			  abfd);
      return -1;
    }
  *s = xmemdup (cursor, s_len + 1, s_len + 1);
  return s_len + 1;
}

/* Parse an object attribute (v2 only).  */
static ssize_t
oav2_parse_attr (bfd *abfd,
		 bfd_byte *cursor,
		 const bfd_byte *end,
		 obj_attr_encoding_v2_t attr_type,
		 obj_attr_v2_t **attr)
{
  *attr = NULL;
  ssize_t total_read = 0;

  obj_attr_tag_t attr_tag;
  READ_ULEB128 (abfd, attr_tag, cursor, end, total_read);

  union obj_attr_value_v2 attr_val;
  switch (attr_type)
    {
    case OA_ENC_NTBS:
      {
	int read = read_ntbs (abfd, cursor, end, (char **) &attr_val.string);
	if (read <= 0)
	  return -1;
	total_read += read;
      }
      break;
    case OA_ENC_ULEB128:
      READ_ULEB128 (abfd, attr_val.uint, cursor, end, total_read);
      break;
    default:
      abort ();
    }

  *attr = bfd_elf_obj_attr_v2_init (attr_tag, attr_val);
  return total_read;
}

/* Parse a subsection (object attributes v2 only).  */
static ssize_t
oav2_parse_subsection (bfd *abfd,
		       bfd_byte *cursor,
		       const uint64_t max_read,
		       obj_attr_subsection_v2_t **subsec)
{
  *subsec = NULL;
  ssize_t total_read = 0;

  char *subsection_name = NULL;
  const uint32_t F_SUBSECTION_LEN = sizeof(uint32_t);
  const uint32_t F_SUBSECTION_COMPREHENSION = sizeof(uint8_t);
  const uint32_t F_SUBSECTION_ENCODING = sizeof(uint8_t);
  /* The minimum subsection length is 7: 4 bytes for the length itself, and 1
     byte for an empty NUL-terminated string, 1 byte for the comprehension,
     1 byte for the encoding, and no vendor-data.  */
  const uint32_t F_MIN_SUBSECTION_DATA_LEN
    = F_SUBSECTION_LEN + 1 /* for '\0' */
      + F_SUBSECTION_COMPREHENSION + F_SUBSECTION_ENCODING;

  /* Similar to the issues reported in PR 17531, we need to check all the sizes
     and offsets as we parse the section.  */
  if (max_read < F_MIN_SUBSECTION_DATA_LEN)
    {
      _bfd_error_handler (_("%pB: error: attributes subsection ends "
			    "prematurely"),
			  abfd);
      goto error;
    }

  const uint32_t subsection_len = bfd_get_32 (abfd, cursor);
  const bfd_byte *const end = cursor + subsection_len;
  total_read += F_SUBSECTION_LEN;
  cursor += F_SUBSECTION_LEN;
  if (subsection_len > max_read)
    {
      _bfd_error_handler (_("%pB: error: bad subsection length (%u > max=%lu)"),
			  abfd, subsection_len, max_read);
      goto error;
    }
  else if (subsection_len < F_MIN_SUBSECTION_DATA_LEN)
    {
      _bfd_error_handler (_("%pB: error: subsection length of %u is too small"),
			  abfd, subsection_len);
      goto error;
    }

  const size_t max_subsection_name_len
    = subsection_len - F_SUBSECTION_LEN
      - F_SUBSECTION_COMPREHENSION - F_SUBSECTION_ENCODING;
  const bfd_byte *subsection_name_end
    = memchr (cursor, '\0', max_subsection_name_len);
  if (subsection_name_end == NULL)
    {
      _bfd_error_handler (_("%pB: error: subsection name seems corrupted "
			    "(missing '\\0')"),
			  abfd);
      goto error;
    }
  else
    /* Move the end pointer after '\0'.  */
    ++subsection_name_end;

  /* Note: if the length of the subsection name is 0 (i.e. the string is '\0'),
     it is still considered a valid name, even if it is not particularly
     useful.  */

  /* Note: at this stage,
     1. the length of the subsection name is validated, as the presence of '\0'
	at the end of the string, so no risk of buffer overrun.
     2. the data for comprehension and encoding can also safely be read.  */
  {
    /* Note: read_ntbs() assigns a dynamically allocated string to
       subsection_name.  Either the string has to be freed in case of errors,
       or its ownership must be transferred.  */
    int read = read_ntbs (abfd, cursor, subsection_name_end, &subsection_name);
    total_read += read;
    cursor += read;
  }

  uint8_t comprehension_raw = bfd_get_8 (abfd, cursor);
  ++cursor;
  ++total_read;

  /* Comprehension is supposed to be a boolean, so any value greater than 1 is
     considered invalid.  */
  if (comprehension_raw > 1)
    {
      _bfd_error_handler (_("%pB: error: '%s' seems corrupted, got %u but only "
			    "0 ('%s') or 1 ('%s') are valid values"),
			  abfd, "comprehension", comprehension_raw,
			  "required", "optional");
      goto error;
    }

  uint8_t value_encoding_raw = bfd_get_8 (abfd, cursor);
  ++cursor;
  ++total_read;

  /* Encoding cannot be greater than OA_ENC_MAX, otherwise it means that either
     there is a new encoding that was introduced in the spec, and this
     implementation in binutils is older, and not aware of it so does not
     support it; or the stored value for encoding is garbage.  */
  enum obj_attr_encoding_v2 value_encoding
    = obj_attr_encoding_v2_from_u8 (value_encoding_raw);
  if (value_encoding > OA_ENC_MAX)
    {
      _bfd_error_handler (_("%pB: error: attribute type seems corrupted, got"
			    " %u but only 0 (ULEB128) or 1 (NTBS) are "
			    "valid types"),
			  abfd, value_encoding);
      goto error;
    }

  obj_attr_subsection_scope_v2_t scope
    = bfd_elf_obj_attr_subsection_v2_scope (abfd, subsection_name);

  /* Note: ownership of 'subsection_name' is transfered to the callee when
     initializing the subsection.  That is why we skip free() at the end.  */
  *subsec = bfd_elf_obj_attr_subsection_v2_init
    (subsection_name, scope, comprehension_raw, value_encoding);

  /* A subsection can be empty, so 'cursor' can be equal to 'end' here.  */
  bool err = false;
  while (!err && cursor < end)
    {
      obj_attr_v2_t *attr;
      ssize_t read = oav2_parse_attr (abfd, cursor, end, value_encoding, &attr);
      if (attr != NULL)
	LINKED_LIST_APPEND (obj_attr_v2_t) (*subsec, attr);
      total_read += read;
      err |= (read < 0);
      cursor += read;
    }

  if (err)
    {
      _bfd_elf_obj_attr_subsection_v2_free (*subsec);
      *subsec = NULL;
      return -1;
    }

  BFD_ASSERT (cursor == end);
  return total_read;

 error:
  bfd_set_error (bfd_error_malformed_archive);
  if (subsection_name)
    free (subsection_name);
  return -1;
}

/* Parse the list of subsections (object attributes v2 only).  */
static void
oav2_parse_section (bfd *abfd,
		    const Elf_Internal_Shdr *hdr,
		    bfd_byte *cursor)
{
  obj_attr_subsection_list_t *subsecs = &elf_obj_attr_subsections (abfd);
  ssize_t read = 0;
  for (uint64_t remaining = hdr->sh_size - 1; /* Already read 'A'.  */
       remaining > 0;
       remaining -= read, cursor += read)
    {
      obj_attr_subsection_v2_t *subsec = NULL;
      read = oav2_parse_subsection (abfd, cursor, remaining, &subsec);
      if (read < 0)
	{
	  _bfd_error_handler
	    (_("%pB: error: could not parse subsection at offset %" PRIx64),
	     abfd, hdr->sh_size - remaining);
	  bfd_set_error (bfd_error_wrong_format);
	  break;
	}
      else
	LINKED_LIST_APPEND (obj_attr_subsection_v2_t) (subsecs, subsec);
    }
}

/* Parse an object attributes section.
   Note: The parsing setup is common between object attributes v1 and v2.  */
void
_bfd_elf_parse_attributes (bfd *abfd, Elf_Internal_Shdr * hdr)
{
  elf_obj_attr_version (abfd) = OBJ_ATTR_VERSION_NONE;

  /* PR 17512: file: 2844a11d.  */
  if (hdr->sh_size == 0)
    return;

  ufile_ptr filesize = bfd_get_file_size (abfd);
  if (filesize != 0 && hdr->sh_size > filesize)
    {
      _bfd_error_handler
	(_("%pB: error: attribute section '%pA' too big: %" PRId64),
	 abfd, hdr->bfd_section, hdr->sh_size);
      bfd_set_error (bfd_error_invalid_operation);
      return;
    }

  bfd_byte *data = (bfd_byte *) bfd_malloc (hdr->sh_size);
  if (!data)
    return;

  if (!bfd_get_section_contents (abfd, hdr->bfd_section, data, 0, hdr->sh_size))
    goto free_data;

  unsigned char *cursor = data;

  /* The first character is the version of the attributes.  */
  obj_attr_version_t version
    = get_elf_backend_data (abfd)->obj_attrs_version_dec (*cursor);
  if (version == OBJ_ATTR_VERSION_UNSUPPORTED || version > OBJ_ATTR_VERSION_MAX)
    {
      _bfd_error_handler (_("%pB: error: unknown attributes version '%c'(%d)\n"),
			  abfd, *cursor, *cursor);
      bfd_set_error (bfd_error_wrong_format);
      goto free_data;
    }

  ++cursor;

  elf_obj_attr_version (abfd) = version;
  switch (version)
    {
    case OBJ_ATTR_V1:
      oav1_parse_section (abfd, cursor, data + hdr->sh_size);
      break;
    case OBJ_ATTR_V2:
      oav2_parse_section (abfd, hdr, cursor);
      break;
    default:
      abort ();
    }

 free_data:
  free (data);
}

/* Merge common object attributes from IBFD into OBFD.  Raise an error
   if there are conflicting attributes.  Any processor-specific
   attributes have already been merged.  This must be called from the
   bfd_elfNN_bfd_merge_private_bfd_data hook for each individual
   target, along with any target-specific merging.  Because there are
   no common attributes other than Tag_compatibility at present, and
   non-"gnu" Tag_compatibility is not expected in "gnu" sections, this
   is not presently called for targets without their own
   attributes.  */

bool
_bfd_elf_merge_object_attributes (bfd *ibfd, struct bfd_link_info *info)
{
  bfd *obfd = info->output_bfd;
  obj_attribute *in_attr;
  obj_attribute *out_attr;
  int vendor;

  /* Set the object attribute version for the output object to the recommended
     value by the backend.  */
  elf_obj_attr_version (obfd)
    = get_elf_backend_data (obfd)->default_obj_attr_version;

  /* The only common attribute is currently Tag_compatibility,
     accepted in both processor and "gnu" sections.  */
  for (vendor = OBJ_ATTR_FIRST; vendor <= OBJ_ATTR_LAST; vendor++)
    {
      /* Handle Tag_compatibility.  The tags are only compatible if the flags
	 are identical and, if the flags are '1', the strings are identical.
	 If the flags are non-zero, then we can only use the string "gnu".  */
      in_attr = &elf_known_obj_attributes (ibfd)[vendor][Tag_compatibility];
      out_attr = &elf_known_obj_attributes (obfd)[vendor][Tag_compatibility];

      if (in_attr->i > 0 && strcmp (in_attr->s, "gnu") != 0)
	{
	  _bfd_error_handler
	    /* xgettext:c-format */
	    (_("error: %pB: object has vendor-specific contents that "
	       "must be processed by the '%s' toolchain"),
	     ibfd, in_attr->s);
	  return false;
	}

      if (in_attr->i != out_attr->i
	  || (in_attr->i != 0 && strcmp (in_attr->s, out_attr->s) != 0))
	{
	  /* xgettext:c-format */
	  _bfd_error_handler (_("error: %pB: object tag '%d, %s' is "
				"incompatible with tag '%d, %s'"),
			      ibfd,
			      in_attr->i, in_attr->s ? in_attr->s : "",
			      out_attr->i, out_attr->s ? out_attr->s : "");
	  return false;
	}
    }

  return true;
}

/* Merge an unknown processor-specific attribute TAG, within the range
   of known attributes, from IBFD into OBFD; return TRUE if the link
   is OK, FALSE if it must fail.  */

bool
_bfd_elf_merge_unknown_attribute_low (bfd *ibfd, bfd *obfd, int tag)
{
  obj_attribute *in_attr;
  obj_attribute *out_attr;
  bfd *err_bfd = NULL;
  bool result = true;

  in_attr = elf_known_obj_attributes_proc (ibfd);
  out_attr = elf_known_obj_attributes_proc (obfd);

  if (out_attr[tag].i != 0 || out_attr[tag].s != NULL)
    err_bfd = obfd;
  else if (in_attr[tag].i != 0 || in_attr[tag].s != NULL)
    err_bfd = ibfd;

  if (err_bfd != NULL)
    result
      = get_elf_backend_data (err_bfd)->obj_attrs_handle_unknown (err_bfd, tag);

  /* Only pass on attributes that match in both inputs.  */
  if (in_attr[tag].i != out_attr[tag].i
      || (in_attr[tag].s == NULL) != (out_attr[tag].s == NULL)
      || (in_attr[tag].s != NULL && out_attr[tag].s != NULL
	  && strcmp (in_attr[tag].s, out_attr[tag].s) != 0))
    {
      out_attr[tag].i = 0;
      out_attr[tag].s = NULL;
    }

  return result;
}

/* Merge the lists of unknown processor-specific attributes, outside
   the known range, from IBFD into OBFD; return TRUE if the link is
   OK, FALSE if it must fail.  */

bool
_bfd_elf_merge_unknown_attribute_list (bfd *ibfd, bfd *obfd)
{
  obj_attribute_list *in_list;
  obj_attribute_list *out_list;
  obj_attribute_list **out_listp;
  bool result = true;

  in_list = elf_other_obj_attributes_proc (ibfd);
  out_listp = &elf_other_obj_attributes_proc (obfd);
  out_list = *out_listp;

  for (; in_list || out_list; )
    {
      bfd *err_bfd = NULL;
      unsigned int err_tag = 0;

      /* The tags for each list are in numerical order.  */
      /* If the tags are equal, then merge.  */
      if (out_list && (!in_list || in_list->tag > out_list->tag))
	{
	  /* This attribute only exists in obfd.  We can't merge, and we don't
	     know what the tag means, so delete it.  */
	  err_bfd = obfd;
	  err_tag = out_list->tag;
	  *out_listp = out_list->next;
	  out_list = *out_listp;
	}
      else if (in_list && (!out_list || in_list->tag < out_list->tag))
	{
	  /* This attribute only exists in ibfd. We can't merge, and we don't
	     know what the tag means, so ignore it.  */
	  err_bfd = ibfd;
	  err_tag = in_list->tag;
	  in_list = in_list->next;
	}
      else /* The tags are equal.  */
	{
	  /* As present, all attributes in the list are unknown, and
	     therefore can't be merged meaningfully.  */
	  err_bfd = obfd;
	  err_tag = out_list->tag;

	  /*  Only pass on attributes that match in both inputs.  */
	  if (in_list->attr.i != out_list->attr.i
	      || (in_list->attr.s == NULL) != (out_list->attr.s == NULL)
	      || (in_list->attr.s && out_list->attr.s
		  && strcmp (in_list->attr.s, out_list->attr.s) != 0))
	    {
	      /* No match.  Delete the attribute.  */
	      *out_listp = out_list->next;
	      out_list = *out_listp;
	    }
	  else
	    {
	      /* Matched.  Keep the attribute and move to the next.  */
	      out_list = out_list->next;
	      in_list = in_list->next;
	    }
	}

      if (err_bfd)
	result = result
	  && get_elf_backend_data (err_bfd)->obj_attrs_handle_unknown (err_bfd,
								       err_tag);
    }

  return result;
}

/* Create a new object attribute with key TAG and value VAL.
   Return a pointer to it.  */

obj_attr_v2_t *
bfd_elf_obj_attr_v2_init (obj_attr_tag_t tag,
			  union obj_attr_value_v2 val)
{
  obj_attr_v2_t *attr = XCNEW (obj_attr_v2_t);
  attr->tag = tag;
  attr->val = val;
  attr->status = obj_attr_v2_ok;
  return attr;
}

/* Free memory allocated by the object attribute ATTR.  */

void
_bfd_elf_obj_attr_v2_free (obj_attr_v2_t *attr, obj_attr_encoding_v2_t encoding)
{
  if (encoding == OA_ENC_NTBS)
    /* Note: this field never holds a string literal.  */
    free ((char *) attr->val.string);
  free (attr);
}

/* Copy an object attribute OTHER, and return a pointer to the copy.  */

obj_attr_v2_t *
_bfd_elf_obj_attr_v2_copy (const obj_attr_v2_t *other,
			   obj_attr_encoding_v2_t encoding)
{
  union obj_attr_value_v2 val;
  if (encoding == OA_ENC_NTBS)
    val.string
      = (other->val.string != NULL
	 ? xstrdup (other->val.string)
	 : NULL);
  else if (encoding == OA_ENC_ULEB128)
    val.uint = other->val.uint;
  else
    abort ();

  obj_attr_v2_t *copy = bfd_elf_obj_attr_v2_init (other->tag, val);
  copy->status = other->status;
  return copy;
}

/* Compare two object attributes based on their TAG value only (partial
   ordering), and return an integer indicating the result of the comparison,
   as follows:
   - 0, if A1 and A2 are equal.
   - a negative value if A1 is less than A2.
   - a positive value if A1 is greater than A2.  */

int
_bfd_elf_obj_attr_v2_cmp (const obj_attr_v2_t *a1, const obj_attr_v2_t *a2)
{
  if (a1->tag < a2->tag)
    return -1;
  if (a1->tag > a2->tag)
    return 1;
  return 0;
}

/* Return an object attribute in SUBSEC matching TAG or NULL if one is not
   found.  SORTED specifies whether the given list is ordered by tag number.
   This allows an early return if we find a higher numbered tag.  */

obj_attr_v2_t *
_bfd_obj_attr_v2_find_by_tag (const obj_attr_subsection_v2_t *subsec,
			      obj_attr_tag_t tag,
			      bool sorted)
{
  for (obj_attr_v2_t *attr = subsec->first;
       attr != NULL;
       attr = attr->next)
    {
      if (attr->tag == tag)
	return attr;
      if (sorted && attr->tag > tag)
	break;
    }
  return NULL;
}

/* Sort the object attributes inside a subsection.
   Note: since a subsection is a list of attributes, the sorting algorithm is
   implemented with a merge sort.
   See more details in libiberty/doubly-linked-list.h  */

LINKED_LIST_MUTATIVE_OPS_DECL (obj_attr_subsection_v2_t,
			       obj_attr_v2_t, /* extern */)
LINKED_LIST_MERGE_SORT_DECL (obj_attr_subsection_v2_t,
			     obj_attr_v2_t, /* extern */)

/* Create a new object attribute subsection with the following properties:
   - NAME: the name of the subsection.  Note: this parameter never holds a
     string literal, so the value has to be freeable.
   - SCOPE: the scope of the subsection (public or private).
   - OPTIONAL: whether this subsection is optional (true) or required (false).
   - ENCODING: the expected encoding for the attributes values (ULEB128 or NTBS).
   Return a pointer to it.  */

obj_attr_subsection_v2_t *
bfd_elf_obj_attr_subsection_v2_init (const char *name,
				      obj_attr_subsection_scope_v2_t scope,
				      bool optional,
				      obj_attr_encoding_v2_t encoding)
{
  obj_attr_subsection_v2_t *subsection = XCNEW (obj_attr_subsection_v2_t);
  subsection->name = name;
  subsection->scope = scope;
  subsection->optional = optional;
  subsection->encoding = encoding;
  subsection->status = obj_attr_subsection_v2_ok;
  return subsection;
}

/* Free memory allocated by the object attribute subsection SUBSEC.  */

void
_bfd_elf_obj_attr_subsection_v2_free (obj_attr_subsection_v2_t *subsec)
{
  obj_attr_v2_t *attr = subsec->first;
  while (attr != NULL)
    {
      obj_attr_v2_t *a = attr;
      attr = attr->next;
      _bfd_elf_obj_attr_v2_free (a, subsec->encoding);
    }
  /* Note: this field never holds a string literal.  */
  free ((char *) subsec->name);
  free (subsec);
}

/* Deep copy an object attribute subsection OTHER, and return a pointer to the
   copy.  */

static obj_attr_subsection_v2_t *
oav2_obj_attr_subsection_v2_copy (const obj_attr_subsection_v2_t *other)
{
  obj_attr_subsection_v2_t *new_subsec
    = bfd_elf_obj_attr_subsection_v2_init (xstrdup (other->name), other->scope,
					    other->optional, other->encoding);
  new_subsec->status = other->status;

  for (obj_attr_v2_t *attr = other->first;
       attr != NULL;
       attr = attr->next)
    {
      obj_attr_v2_t *new_attr = _bfd_elf_obj_attr_v2_copy (attr, other->encoding);
      LINKED_LIST_APPEND (obj_attr_v2_t) (new_subsec, new_attr);
    }
  return new_subsec;
}

/* Compare two object attribute subsections based on all their properties.
   This operator can be used to obtain a total order in a collection.
   Return an integer indicating the result of the comparison, as follows:
   - 0, if S1 and S2 are equal.
   - a negative value if S1 is less than S2.
   - a positive value if S1 is greater than S2.

   NB: the scope is computed from the name, so is not used for the
   comparison.  */

int
_bfd_elf_obj_attr_subsection_v2_cmp (const obj_attr_subsection_v2_t *s1,
				     const obj_attr_subsection_v2_t *s2)
{
  int res = strcmp (s1->name, s2->name);
  if (res != 0)
    return res;

  /* Note: The comparison of the encoding and optionality of subsections
     is entirely arbitrary.  The numeric values could be completely flipped
     around without any effect.  Likewise, assigning higher priority to
     optionality than to encoding is artificial.  The desired properties for
     this comparison operator are reflexivity, transitivity, antisymmetry,
     and totality, in order to achieve a total ordering when sorting a
     collection of subsections.
     If the nature of this ordering were to change in the future, it would
     have no functional impact (but e.g. testsuite expectations might still
     need adjusting) on the final merged result in the output file.  Only the
     order of the serialized subsections would differ, which does not affect
     the interpretation of the object attributes.
     Similarly, the ordering of subsections and attributes in an input file
     does not affect the merge process in ld.  The merge process never assumes
     any particular ordering from the input files, it always sorts the
     subsections and attributes before merging.  This means that using an
     older version of gas with a newer ld is safe, and vice versa as long as
     no new features are used that the older ld doesn't know of.
     In conclusion, the (arbitrary) criteria used to sort subsections during
     the merge process are entirely internal to ld and have no effect on the
     merge result.  */

  if (!s1->optional && s2->optional)
    return -1;
  else if (s1->optional && !s2->optional)
    return 1;

  if (s1->encoding < s2->encoding)
    return -1;
  else if (s1->encoding > s2->encoding)
    return 1;

  return 0;
}

/* Return a subsection in the list FIRST matching NAME, or NULL if one is not
   found.  SORTED specifies whether the given list is ordered by name.
   This allows an early return if we find a alphabetically-higher name.  */

obj_attr_subsection_v2_t *
bfd_obj_attr_subsection_v2_find_by_name (obj_attr_subsection_v2_t *first,
					 const char *name,
					 bool sorted)
{
  for (obj_attr_subsection_v2_t *s = first;
       s != NULL;
       s = s->next)
    {
      int cmp = strcmp (s->name, name);
      if (cmp == 0)
	return s;
      else if (sorted && cmp > 0)
	break;
    }
  return NULL;
}

/* Sort the subsections in a vendor section.
   Note: since a section is a list of subsections, the sorting algorithm is
   implemented with a merge sort.
   See more details in libiberty/doubly-linked-list.h  */

LINKED_LIST_MUTATIVE_OPS_DECL (obj_attr_subsection_list_t,
			       obj_attr_subsection_v2_t, /* extern */)
LINKED_LIST_MERGE_SORT_DECL (obj_attr_subsection_list_t,
			     obj_attr_subsection_v2_t, /* extern */)

/* Serialize the object attributes in ABFD into the vendor section of
   OUTPUT_BFD.  */

bool _bfd_elf_write_section_object_attributes
  (bfd *abfd, struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  asection *sec = elf_obj_object_attributes (abfd);

  if (sec == NULL)
    return true;

  bfd_byte *contents = (bfd_byte *) bfd_malloc (sec->size);
  if (contents == NULL)
    return false; /* Bail out and fail.  */

  bfd_elf_set_obj_attr_contents (abfd, contents, sec->size);
  bfd_set_section_contents (abfd, sec, contents, 0, sec->size);
  free (contents);
  return true;
}
