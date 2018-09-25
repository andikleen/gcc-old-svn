/* Insert instructions for data value tracing.
   Copyright (C) 2017 Free Software Foundation, Inc.
   Contributed by Andi Kleen.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "tree.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "basic-block.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimplify.h"
#include "gimplify-me.h"
#include "gimple-ssa.h"
#include "gimple-pretty-print.h"
#include "cfghooks.h"
#include "ssa.h"
#include "tree-dfa.h"
#include "attribs.h"

enum attrstate { force_off, force_on, neutral };

/* Can we trace with attributes ATTR.  */

static attrstate supported_attr (tree attr)
{
  if (lookup_attribute ("no_vartrace", attr))
    return force_off;
  if (lookup_attribute ("vartrace", attr))
    return force_on;
  return neutral;
}

/* Is ARG supported considering S, handling both decls and other trees.  */

static attrstate supported_op (tree arg, attrstate s)
{
  if (s != neutral)
    return s;
  if (DECL_P (arg))
    {
      s = supported_attr (DECL_ATTRIBUTES (arg));
      if (s != neutral)
	return s;
    }
  return supported_attr (TYPE_ATTRIBUTES (TREE_TYPE (arg)));
}

/* Can we trace T.  */

static attrstate supported_type (tree t)
{
  tree type = TREE_TYPE (t);

  if (!POINTER_TYPE_P (type) && !INTEGRAL_TYPE_P (type))
    return force_off;
  enum attrstate s = supported_op (t, neutral);
  if (TREE_CODE (t) == COMPONENT_REF
	   || TREE_CODE (t) == ARRAY_REF)
    {
      s = supported_op (TREE_OPERAND (t, 0), s);
      s = supported_op (TREE_OPERAND (t, 1), s);
    }
  return s;
}

/* Can we trace T, or if FORCE is set.  */

static bool supported_type_or_force (tree t, bool force)
{
  enum attrstate s = supported_type (t);
  if (s == neutral)
    return force;
  return s == force_off ? false : true;
}

/* Return true if T refering to a local variable.
   ?? better ways to do this?  */

static bool is_local (tree t)
{
  // Add another attribute to override?
  if (!flag_vartrace_locals)
    return false;
  if (TREE_STATIC (t))
    return false;
  if (TREE_CODE (t) == VAR_DECL && DECL_EXTERNAL (t))
    return false;
  return true;
}

/* Is T something we can log, FORCEing the type if needed.  */

static bool supported_mem (tree t, bool force)
{
  enum attrstate s = supported_type (t);

  if (s == force_off)
    return false;

  switch (TREE_CODE (t))
    {
    case VAR_DECL:
      if (DECL_ARTIFICIAL (t))
	return false;
      if (is_local (t))
	return true;
      return s == force_on || force;

    case ARRAY_REF:
    case COMPONENT_REF:
      t = TREE_OPERAND (t, 0);
      if (is_local (t))
	return true;
      return s == force_on || force;

    case TARGET_MEM_REF:
    case MEM_REF:
      // could use points-to to check for locals?
      return true;

    case SSA_NAME:
      if (flag_vartrace_locals && is_gimple_reg (t))
	return true;
      break;

    default:
      break;
    }

  return false;
}

/* Print debugging for inserting CALL at ORIG_STMT with type of VAL.  */

static void log_trace_code (gimple *orig_stmt, gimple *code,
			    tree val)
{
  if (dump_file)
    {
      if (orig_stmt)
	fprintf (dump_file, "BB%d ", gimple_bb (orig_stmt)->index);
      fprintf (dump_file, "inserting ");
      print_gimple_stmt (dump_file, code, 0, TDF_VOPS|TDF_MEMSYMS);
      if (orig_stmt)
	{
	  fprintf (dump_file, "orig ");
	  print_gimple_stmt (dump_file, orig_stmt, 2,
			     TDF_VOPS|TDF_MEMSYMS);
	}
      fprintf (dump_file, "type ");
      print_generic_expr (dump_file, TREE_TYPE (val), TDF_SLIM);
      fputc ('\n', dump_file);
      fputc ('\n', dump_file);
    }
}

/* Insert variable tracing code for VAL before iterator GI, originally
   for ORIG_STMT.  Return trace variable or NULL.  */

static tree insert_trace (gimple_stmt_iterator *gi, tree val,
			  gimple *orig_stmt)
{
  tree func = targetm.vartrace_func (TREE_TYPE (val));
  if (!func)
    return NULL_TREE;

  location_t loc = gimple_location (orig_stmt);

  gimple_seq seq = NULL;
  tree tvar = make_ssa_name (TREE_TYPE (val));
  gassign *assign = gimple_build_assign (tvar, val);
  log_trace_code (orig_stmt, assign, val);
  gimple_set_location (assign, loc);
  gimple_seq_add_stmt (&seq, assign);

  gcall *call = gimple_build_call (func, 1, tvar);
  log_trace_code (NULL, call, tvar);
  gimple_set_location (call, loc);
  gimple_seq_add_stmt (&seq, call);

  gsi_insert_seq_before (gi, seq, GSI_SAME_STMT);
  return tvar;
}

/* Insert trace at GI for T in FUN if suitable memory or variable reference.
   Always if FORCE. Originally on ORIG_STMT.  */

tree instrument_mem (gimple_stmt_iterator *gi, tree t,
		     bool force,
		     gimple *orig_stmt)
{
  if (supported_mem (t, force))
    return insert_trace (gi, t, orig_stmt);
  return NULL_TREE;
}

/* Instrument arguments for FUN considering FORCE. Return true if
   function has changed.  */

bool instrument_args (function *fun, bool force)
{
  bool changed = false;
  gimple_stmt_iterator gi;

  /* Local tracing usually takes care of the argument too, when
     they are read. This avoids redundant trace instructions.  */
  if (flag_vartrace_locals)
    return false;

  for (tree arg = DECL_ARGUMENTS (current_function_decl);
       arg != NULL_TREE;
       arg = DECL_CHAIN (arg))
    {
     gi = gsi_start_bb (BASIC_BLOCK_FOR_FN (fun, NUM_FIXED_BLOCKS));
     if (supported_type_or_force (arg, force || flag_vartrace_args))
	{
	  tree func = targetm.vartrace_func (TREE_TYPE (arg));
	  if (!func)
	    continue;

	  tree sarg = ssa_default_def (fun, arg);
	  /* This can happen with tail recursion. Don't log in this
	     case for now.  */
	  if (!sarg)
	    continue;

	  if (has_zero_uses (sarg))
	    continue;

	  gimple_seq seq = NULL;
	  tree tvar = make_ssa_name (TREE_TYPE (sarg));
	  gassign *assign = gimple_build_assign (tvar, sarg);
	  gimple_set_location (assign, fun->function_start_locus);
	  gimple_seq_add_stmt (&seq, assign);

	  gcall *call = gimple_build_call (func, 1, tvar);
	  log_trace_code (NULL, call, tvar);
	  gimple_set_location (call, fun->function_start_locus);
	  gimple_seq_add_stmt (&seq, call);

	  edge edge = single_succ_edge (ENTRY_BLOCK_PTR_FOR_FN (fun));
	  gsi_insert_seq_on_edge_immediate (edge, seq);

	  changed = true;
	}
    }
  return changed;
}

/* Generate trace call for store STMT at GI, force if FORCE.  Return true
   if successfull. Modifies the original store to use a temporary.  */

static bool instrument_store (gimple_stmt_iterator *gi, gimple *stmt, bool force)
{
  if (!supported_mem (gimple_assign_lhs (stmt), force))
    return false;

  tree orig_tgt = gimple_assign_lhs (stmt);

  tree func = targetm.vartrace_func (TREE_TYPE (orig_tgt));
  if (!func)
    return false;

  tree new_tgt = make_ssa_name(TREE_TYPE (orig_tgt));
  gimple_assign_set_lhs (stmt, new_tgt);
  update_stmt (stmt);
  log_trace_code (NULL, stmt, new_tgt);

  gcall *tcall = gimple_build_call (func, 1, new_tgt);
  log_trace_code (stmt, tcall, new_tgt);
  gimple_set_location (tcall, gimple_location (stmt));
  gsi_insert_after (gi, tcall, GSI_CONTINUE_LINKING);

  gassign *new_store = gimple_build_assign (orig_tgt, new_tgt);
  gimple_set_location (new_store, gimple_location (stmt));
  log_trace_code (NULL, new_store, new_tgt);
  gsi_insert_after (gi, new_store, GSI_CONTINUE_LINKING);
  return true;
}

/* Instrument STMT at GI. Force if FORCE. CHANGED is the previous changed
   state, which is also returned.  */

bool instrument_assign (gimple_stmt_iterator *gi,
			gimple *stmt, bool changed, bool force)
{
  gassign *gas = as_a <gassign *> (stmt);
  bool read_force = force || flag_vartrace_reads;
  tree t;

  t = instrument_mem (gi, gimple_assign_rhs1 (gas),
		      read_force,
		      stmt);
  if (t)
    {
      gimple_assign_set_rhs1 (gas, t);
      changed = true;
    }
  if (gimple_num_ops (gas) > 2)
    {
      t = instrument_mem (gi, gimple_assign_rhs2 (gas),
			  read_force,
			  stmt);
      if (t)
	{
	  gimple_assign_set_rhs2 (gas, t);
	  changed = true;
	}
    }
  if (gimple_num_ops (gas) > 3)
    {
      t = instrument_mem (gi, gimple_assign_rhs3 (gas),
			  read_force,
			  stmt);
      if (t)
	{
	  gimple_assign_set_rhs3 (gas, t);
	  changed = true;
	}
      }
  if (gimple_num_ops (gas) > 4)
    gcc_unreachable ();
  if (gimple_store_p (stmt))
    changed |= instrument_store (gi, stmt, flag_vartrace_writes || force);
  if (changed)
    update_stmt (stmt);
  return changed;
}

/* Instrument return in function FUN at statement STMT at GI, force if
   FORCE.  CHANGED is the changed flag, which is also returned.  */

static bool instrument_return (function *fun,
			       gimple_stmt_iterator *gi,
			       gimple *stmt, bool changed,
			       bool force)
{
  tree restype = TREE_TYPE (TREE_TYPE (fun->decl));
  greturn *gret = as_a <greturn *> (stmt);
  tree rval = gimple_return_retval (gret);

  /* Cannot handle complex C++ return values at this point, even
     if they would collapse to a valid trace type.  */
  if (rval
      && useless_type_conversion_p (restype, TREE_TYPE (rval))
      && supported_type_or_force (rval, flag_vartrace_returns || force))
    {
      if (tree tvar = insert_trace (gi, rval, stmt))
	{
	  changed = true;
	  gimple_return_set_retval (gret, tvar);
	  log_trace_code (NULL, gret, tvar);
	  update_stmt (stmt);
	}
    }

  return changed;
}

/* Insert vartrace calls for FUN.  */

static unsigned int vartrace_execute (function *fun)
{
  basic_block bb;
  gimple_stmt_iterator gi;
  bool force = flag_vartrace;
  bool changed;

  if (lookup_attribute ("vartrace", TYPE_ATTRIBUTES (TREE_TYPE (fun->decl)))
      || lookup_attribute ("vartrace", DECL_ATTRIBUTES (fun->decl)))
    force = true;

  changed = instrument_args (fun, force);

  FOR_ALL_BB_FN (bb, fun)
    for (gi = gsi_start_bb (bb); !gsi_end_p (gi); gsi_next (&gi))
      {
	gimple *stmt = gsi_stmt (gi);
	if (is_gimple_assign (stmt) && !gimple_clobber_p (stmt))
	  changed = instrument_assign (&gi, stmt, changed, force);
	else if (gimple_code (stmt) == GIMPLE_RETURN)
	  {
	    changed = instrument_return (fun, &gi, stmt, changed, force);
	    // must end basic block
	    break;
	  }

	// instrument something else like CALL?
	// We assume everything interesting is in a GIMPLE_ASSIGN
      }
  return changed ? TODO_update_ssa : 0;
}

const pass_data pass_data_vartrace =
{
  GIMPLE_PASS, /* type */
  "vartrace", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_vartrace : public gimple_opt_pass
{
public:
  pass_vartrace (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_vartrace, ctxt)
  {}

  virtual opt_pass * clone ()
    {
      return new pass_vartrace (m_ctxt);
    }

  virtual bool gate (function *fun)
    {
      // check if vartrace is supported in backend
      if (!targetm.vartrace_func ||
	  targetm.vartrace_func (integer_type_node) == NULL)
	return false;

      if (lookup_attribute ("no_vartrace", TYPE_ATTRIBUTES (TREE_TYPE (fun->decl)))
	  || lookup_attribute ("no_vartrace", DECL_ATTRIBUTES (fun->decl)))
	return false;

      // need to run pass always to check for variable attributes
      return true;
    }

  virtual unsigned int execute (function *f) { return vartrace_execute (f); }
};

gimple_opt_pass *
make_pass_vartrace (gcc::context *ctxt)
{
  return new pass_vartrace (ctxt);
}
