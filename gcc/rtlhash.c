/* RTL hash functions.
   Copyright (C) 1987-2014 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "rtl.h"
#include "rtlhash.h"

/* Iteratively hash rtx X into HSTATE.  */

void
iterative_hstate_rtx (const_rtx x, inchash &hstate)
{
  enum rtx_code code;
  enum machine_mode mode;
  int i, j;
  const char *fmt;

  if (x == NULL_RTX)
    return;
  code = GET_CODE (x);
  hstate.add_object (code);
  mode = GET_MODE (x);
  hstate.add_object (mode);
  switch (code)
    {
    case REG:
      hstate.add_int (REGNO (x));
      return;
    case CONST_INT:
      hstate.add_object (INTVAL (x));
      return;
    case CONST_WIDE_INT:
      for (i = 0; i < CONST_WIDE_INT_NUNITS (x); i++)
	hstate.add_object (CONST_WIDE_INT_ELT (x, i));
      return;
    case SYMBOL_REF:
      if (XSTR (x, 0))
	hstate.add (XSTR (x, 0), strlen (XSTR (x, 0)) + 1);
      return;
    case LABEL_REF:
    case DEBUG_EXPR:
    case VALUE:
    case SCRATCH:
    case CONST_DOUBLE:
    case CONST_FIXED:
    case DEBUG_IMPLICIT_PTR:
    case DEBUG_PARAMETER_REF:
      return;
    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    switch (fmt[i])
      {
      case 'w':
	hstate.add_object (XWINT (x, i));
	break;
      case 'n':
      case 'i':
	hstate.add_object (XINT (x, i));
	break;
      case 'V':
      case 'E':
	j = XVECLEN (x, i);
	hstate.add_int (j);
	for (j = 0; j < XVECLEN (x, i); j++)
	  iterative_hstate_rtx (XVECEXP (x, i, j), hstate);
	break;
      case 'e':
	iterative_hstate_rtx (XEXP (x, i), hstate);
	break;
      case 'S':
      case 's':
	if (XSTR (x, i))
	  hstate.add (XSTR (x, 0), strlen (XSTR (x, 0)) + 1);
	break;
      default:
	break;
      }
}
