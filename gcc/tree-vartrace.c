/* Insert instructions for data value tracing.
   Copyright (C) 2017, 2018 Free Software Foundation, Inc.
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
#include "fold-const.h"
#include "ssa.h"
#include "tree-dfa.h"
#include "attribs.h"

namespace {

enum attrstate { force_off, force_on, neutral };

/* Can we trace with attributes ATTR.  */

attrstate
supported_attr (tree attr)
{
  if (lookup_attribute ("no_vartrace", attr))
    return force_off;
  if (lookup_attribute ("vartrace", attr))
    return force_on;
  return neutral;
}

/* Is tracing enabled for ARG considering S.  */

attrstate
supported_op (tree arg, attrstate s)
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

/* Can we trace DECL.  */

bool
supported_type (tree decl)
{
  tree type = TREE_TYPE (decl);

  return POINTER_TYPE_P (type) || INTEGRAL_TYPE_P (type);
}

/* Return true if DECL is refering to a local variable.  */

bool
is_local (tree decl)
{
  if (!(flag_vartrace & VARTRACE_LOCALS) && supported_op (decl, neutral) != force_on)
    return false;
  return auto_var_in_fn_p (decl, cfun->decl);
}

/* Is T something we can log, FORCEing the type if needed.  */

bool
supported_mem (tree t, bool force)
{
  if (!supported_type (t))
    return false;

  enum attrstate s = supported_op (t, neutral);
  if (s == force_off)
    return false;
  if (s == force_on)
    force = true;

  switch (TREE_CODE (t))
    {
    case VAR_DECL:
      if (DECL_ARTIFICIAL (t))
	return false;
      if (is_local (t))
	return true;
      return s == force_on || force;

    case ARRAY_REF:
      t = TREE_OPERAND (t, 0);
      s = supported_op (t, s);
      if (s == force_off)
	return false;
      return supported_type (TREE_TYPE (t));

    case COMPONENT_REF:
      s = supported_op (TREE_OPERAND (t, 1), s);
      t = TREE_OPERAND (t, 0);
      if (s == neutral && is_local (t))
	return true;
      s = supported_op (t, s);
      if (s != neutral)
	return s == force_on ? true : false;
      return supported_mem (t, force);

      // support BIT_FIELD_REF?

    case VIEW_CONVERT_EXPR:
    case TARGET_MEM_REF:
    case MEM_REF:
      return supported_mem (TREE_OPERAND (t, 0), force);

    case SSA_NAME:
      if ((flag_vartrace & VARTRACE_LOCALS)
	  && SSA_NAME_VAR (t)
	  && !DECL_IGNORED_P (SSA_NAME_VAR (t)))
	return true;
      return force;

    default:
      break;
    }

  return false;
}

/* Print debugging for inserting CODE at ORIG_STMT with type of VAL for WHY.  */

void
log_trace_code (gimple *orig_stmt, gimple *code, tree val, const char *why)
{
  if (!dump_file)
    return;
  if (orig_stmt)
    fprintf (dump_file, "BB%d ", gimple_bb (orig_stmt)->index);
  fprintf (dump_file, "%s inserting ", why);
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

/* Insert variable tracing code for VAL before iterator GI, originally
   for ORIG_STMT and optionally at LOC. Normally before ORIG_STMT, but
   AFTER if true. Reason is WHY. Return trace var if successfull,
   or NULL_TREE.  */

tree
insert_trace (gimple_stmt_iterator *gi, tree val, gimple *orig_stmt,
	      const char *why, location_t loc = -1, bool after = false)
{
  if (loc == (location_t)-1)
    loc = gimple_location (orig_stmt);

  tree func = targetm.vartrace_func (TREE_TYPE (val), false);
  if (!func)
    return NULL_TREE;

  tree tvar = val;
  if (!is_gimple_reg (val))
    {
      tvar = make_ssa_name (TREE_TYPE (val));
      gassign *assign = gimple_build_assign (tvar, unshare_expr (val));
      log_trace_code (orig_stmt, assign, val, "copy");
      gimple_set_location (assign, loc);
      if (after)
	gsi_insert_after (gi, assign, GSI_CONTINUE_LINKING);
      else
	gsi_insert_before (gi, assign, GSI_SAME_STMT);
      update_stmt (assign);
    }

  gcall *call = gimple_build_call (func, 1, tvar);
  log_trace_code (NULL, call, tvar, why);
  gimple_set_location (call, loc);
  if (after)
    gsi_insert_after (gi, call, GSI_CONTINUE_LINKING);
  else
    gsi_insert_before (gi, call, GSI_SAME_STMT);
  update_stmt (call);
  return tvar;
}

/* Insert trace at GI for T in FUN if suitable memory or variable
   reference.  Always if FORCE. Originally on ORIG_STMT. Reason is
   WHY.  Insert after GI if AFTER. Returns trace variable or NULL_TREE.  */

tree
instrument_mem (gimple_stmt_iterator *gi, tree t, bool force,
		gimple *orig_stmt, const char *why, bool after = false)
{
  if (supported_mem (t, force))
    return insert_trace (gi, t, orig_stmt, why, -1, after);
  return NULL_TREE;
}

/* Instrument arguments for FUN. Return true if changed.  */

bool
instrument_args (function *fun)
{
  gimple_stmt_iterator gi;
  bool changed = false;

  /* Local tracing usually takes care of the argument too, when
     they are read. This avoids redundant trace instructions.  */
  if (flag_vartrace & VARTRACE_LOCALS)
    return false;

  for (tree arg = DECL_ARGUMENTS (current_function_decl);
       arg != NULL_TREE;
       arg = DECL_CHAIN (arg))
    {
     gi = gsi_start_bb (BASIC_BLOCK_FOR_FN (fun, NUM_FIXED_BLOCKS));
     tree type = TREE_TYPE (arg);
     if (POINTER_TYPE_P (type) || INTEGRAL_TYPE_P (type))
	{
	  tree func = targetm.vartrace_func (TREE_TYPE (arg), false);
	  if (!func)
	    continue;

	  if (!is_gimple_reg (arg))
	    continue;
	  tree sarg = ssa_default_def (fun, arg);
	  if (!sarg)
	    continue;

	  gimple_stmt_iterator egi = gsi_after_labels (single_succ (ENTRY_BLOCK_PTR_FOR_FN (fun)));
	  changed |= !!insert_trace (&egi, sarg, NULL, "arg", fun->function_start_locus);
	}
    }
  return changed;
}

/* Generate trace call for store GAS at GI, force if FORCE.  Return true
   if successfull. Return true if successfully inserted.  */

bool
instrument_store (gimple_stmt_iterator *gi, gassign *gas, bool force)
{
  tree orig = gimple_assign_lhs (gas);

  if (!supported_mem (orig, force))
    return false;

  tree func = targetm.vartrace_func (TREE_TYPE (orig), false);
  if (!func)
    return false;

  /* Generate another reference to target. That can be racy, but is
     guaranteed to have the debug location of the target.  Better
     would be to use the original value to avoid any races, but we
     would need to somehow force the target location of the
     builtin.  */

  tree tvar = make_ssa_name (TREE_TYPE (orig));
  gassign *assign = gimple_build_assign (tvar, unshare_expr (orig));
  log_trace_code (gas, assign, orig, "store copy");
  gimple_set_location (assign, gimple_location (gas));
  gsi_insert_after (gi, assign, GSI_CONTINUE_LINKING);
  update_stmt (assign);

  gcall *tcall = gimple_build_call (func, 1, tvar);
  log_trace_code (gas, tcall, tvar, "store");
  gimple_set_location (tcall, gimple_location (gas));
  gsi_insert_after (gi, tcall, GSI_CONTINUE_LINKING);
  update_stmt (tcall);
  return true;
}

/* Instrument STMT at GI. Force if FORCE. Return true if changed.  */

bool
instrument_assign (gimple_stmt_iterator *gi, gassign *gas, bool force)
{
  if (gimple_clobber_p (gas))
    return false;
  bool changed = false;
  tree tvar = instrument_mem (gi, gimple_assign_rhs1 (gas),
			      (flag_vartrace & VARTRACE_READS) || force,
			      gas, "assign load1");
  if (tvar)
    {
      gimple_assign_set_rhs1 (gas, tvar);
      changed = true;
    }
  /* Handle operators in case they read locals.  */
  if (gimple_num_ops (gas) > 2)
    {
      tvar = instrument_mem (gi, gimple_assign_rhs2 (gas),
			      (flag_vartrace & VARTRACE_READS) || force,
			      gas, "assign load2");
      if (tvar)
	{
	  gimple_assign_set_rhs2 (gas, tvar);
	  changed = true;
	}
    }
  // handle more ops?

  if (gimple_store_p (gas))
    changed |= instrument_store (gi, gas,
				 (flag_vartrace & VARTRACE_WRITES) || force);

  if (changed)
    update_stmt (gas);
  return changed;
}

/* Instrument return at statement STMT at GI with FORCE. Return true
   if changed.  */

bool
instrument_return (gimple_stmt_iterator *gi, greturn *gret, bool force)
{
  tree rval = gimple_return_retval (gret);

  if (!rval)
    return false;
  if (DECL_P (rval) && DECL_BY_REFERENCE (rval))
    rval = build_simple_mem_ref (ssa_default_def (cfun, rval));
  if (supported_mem (rval, force))
    return !!insert_trace (gi, rval, gret, "return");
  return false;
}

/* Instrument asm at GI in statement STMT with FORCE if needed. Return
   true if changed.  */

bool
instrument_asm (gimple_stmt_iterator *gi, gasm *stmt, bool force)
{
  bool changed = false;

  for (unsigned i = 0; i < gimple_asm_ninputs (stmt); i++)
    changed |= !!instrument_mem (gi, TREE_VALUE (gimple_asm_input_op (stmt, i)),
				 force || (flag_vartrace & VARTRACE_READS), stmt,
				 "asm input");
  for (unsigned i = 0; i < gimple_asm_noutputs (stmt); i++)
    {
      tree o = TREE_VALUE (gimple_asm_output_op (stmt, i));
      if (supported_mem (o, force | (flag_vartrace & VARTRACE_WRITES)))
	  changed |= !!insert_trace (gi, o, stmt, "asm output", -1, true);
    }
  return changed;
}

/* Insert vartrace calls for FUN.  */

unsigned int
vartrace_execute (function *fun)
{
  basic_block bb;
  gimple_stmt_iterator gi;
  bool force = 0;

  if (lookup_attribute ("vartrace", TYPE_ATTRIBUTES (TREE_TYPE (fun->decl)))
      || lookup_attribute ("vartrace", DECL_ATTRIBUTES (fun->decl)))
    force = true;

  bool changed = false;

  if ((flag_vartrace & VARTRACE_ARGS) || force)
    changed |= instrument_args (fun);

  FOR_EACH_BB_FN (bb, fun)
    for (gi = gsi_start_bb (bb); !gsi_end_p (gi); gsi_next (&gi))
      {
	gimple *stmt = gsi_stmt (gi);
	switch (gimple_code (stmt))
	  {
	  case GIMPLE_ASSIGN:
	    changed |= instrument_assign (&gi, as_a <gassign *> (stmt), force);
	    break;
	  case GIMPLE_RETURN:
	    changed |= instrument_return (&gi, as_a <greturn *> (stmt),
					  force || (flag_vartrace & VARTRACE_RETURNS));
	    break;

	    // for GIMPLE_CALL we use the argument logging in the callee
	    // we could optionally log in the caller too to handle all possible
	    // reads of a local/global when the callee is not instrumented
	    // possibly later we could also instrument copy and clear calls.

	  case GIMPLE_SWITCH:
	    changed |= !!instrument_mem (&gi, gimple_switch_index (as_a <gswitch *> (stmt)),
					 force, stmt, "switch");
	    break;
	  case GIMPLE_COND:
	    changed |= !!instrument_mem (&gi, gimple_cond_lhs (stmt), force, stmt, "if lhs");
	    changed |= !!instrument_mem (&gi, gimple_cond_rhs (stmt), force, stmt, "if rhs");
	    break;

	  case GIMPLE_ASM:
	    changed |= instrument_asm (&gi, as_a<gasm *> (stmt), force);
	    break;
	  default:
	    // everything else that reads/writes variables should be lowered already
	    break;
	  }
      }

  // for now, until we fix all cases that destroy ssa
  return changed ? TODO_update_ssa : 0;;
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
      if (!targetm.vartrace_func
	  || targetm.vartrace_func (integer_type_node, false) == NULL)
	return false;

      if (lookup_attribute ("no_vartrace", TYPE_ATTRIBUTES (TREE_TYPE (fun->decl)))
	  || lookup_attribute ("no_vartrace", DECL_ATTRIBUTES (fun->decl)))
	return false;

      // need to run pass always to check for variable attributes
      return true;
    }

  virtual unsigned int execute (function *f) { return vartrace_execute (f); }
};

} // anon namespace

gimple_opt_pass *
make_pass_vartrace (gcc::context *ctxt)
{
  return new pass_vartrace (ctxt);
}
