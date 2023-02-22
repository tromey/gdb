#ifndef GDB_WIDE_INT_H
#define GDB_WIDE_INT_H

#define GTY(X)

typedef LONGEST HOST_WIDE_INT;
typedef ULONGEST UHOST_WIDE_INT;

#define BITS_PER_UNIT HOST_CHAR_BIT
#define UNITS_PER_WORD sizeof (unsigned int)
#define MAX_BITSIZE_MODE_ANY_INT HOST_BITS_PER_WIDE_INT

#define STATIC_ASSERT gdb_static_assert

#define gcc_assert gdb_assert
#define gcc_checking_assert gdb_assert

#if GCC_VERSION >= 3001
#define STATIC_CONSTANT_P(X) (__builtin_constant_p (X) && (X))
#else
#define STATIC_CONSTANT_P(X) (false && (X))
#endif

#define MAX std::max

#define IN_RANGE(VALUE, LOWER, UPPER) \
  ((UHOST_WIDE_INT) (VALUE) - (UHOST_WIDE_INT) (LOWER) \
   <= (UHOST_WIDE_INT) (UPPER) - (UHOST_WIDE_INT) (LOWER))

#define LIKELY(X) (X)

#include <gmp.h>
#include "gcc-imports/signop.h"
#include "gcc-imports/hwint.h"
#include "gcc-imports/wide-int.h"

#endif /* GDB_WIDE_INT_H */
