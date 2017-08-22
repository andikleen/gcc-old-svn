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

/* General theory:

   Insert trace builtins into a function to log data of interest
   for debugging purposes later. We rely on the backend to do
   the logging; currently only through the PTWRITE instruction
   on Intel, which writes data to the CPU's Processor Trace log.

   We support arguments, returns, all memory reads and writes (such as
   pointer references) for user-visible variables, and locals if
   enabled.

   The locals tracing is quite limited and often cannot trace
   everything when optimization is enabled. It may also lead
   to significantly larger code and more run time overhead.

   When a variable has the vartrace attribute specified, it is always
   logged independently of -fvartrace options (unless -fvartrace=off is
   specified which overrides even attributes). Same for types and
   structure members.

   When a function has the vartrace attribute specified, every user visible
   access in it (except locals) is logged, except when -fvartrace=off
   globally overrides it.

   When a variable or type has no_vartrace set it is never logged.

   When a function has no_vartrace set, nothing in it is logged.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "tree.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "tree-eh.h"
#include "basic-block.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimplify.h"
#include "gimplify-me.h"
#include "gimple-ssa.h"
#include "gimple-walk.h"
#include "gimple-pretty-print.h"
#include "cfghooks.h"
#include "fold-const.h"
#include "ssa.h"
#include "tree-dfa.h"
#include "attribs.h"

enum attrstate { force_off, force_on, neutral };

/* Are locals enabled or forced with FLAG (read or write)?  */

static bool
locals_enabled (enum vartrace_flags flag)
{
  return (flag_vartrace & VARTRACE_LOCALS) && (flag_vartrace & flag);
}

/* Log tree OP not being traced to dump_file.  */

static void
log_op (tree op, const char *what)
{
  if (dump_file)
    {
      fprintf (dump_file, "%s ", what);
      print_generic_expr (dump_file, op, TDF_VOPS);
      fprintf (dump_file, " type ");
      print_generic_expr (dump_file, TREE_TYPE (op));
      fputc ('\n', dump_file);
    }
}

/* Log tree OP if verbose dump enabled.  */

static void
log_op_verbose (tree op, const char *what)
{
  if (dump_flags & TDF_DETAILS)
    log_op (op, what);
}

/* Is tracing enabled with attributes ATTR.  */

static attrstate
enabled_attr (tree attr)
{
  if (lookup_attribute ("no_vartrace", attr))
    return force_off;
  if (lookup_attribute ("vartrace", attr))
    return force_on;
  return neutral;
}

/* Is tracing enabled for ARG considering S.  */

static attrstate
enabled_op (tree arg, attrstate s)
{
  if (s != neutral)
    return s;
  if (DECL_P (arg))
    {
      s = enabled_attr (DECL_ATTRIBUTES (arg));
      if (s != neutral)
	return s;
    }
  return enabled_attr (TYPE_ATTRIBUTES (TREE_TYPE (arg)));
}

/* Can we trace OP's type? Return true if yes.  */

static bool
supported_type (tree op)
{
  // right now we can only trace objects that fit into gimple
  // registers. this rejects records, would need special handling
  // in insert_trace for those.
  tree type = TREE_TYPE (op);
  return INTEGRAL_TYPE_P (type) || SCALAR_FLOAT_TYPE_P (type)
	  || POINTER_TYPE_P (type);
}

/* Is OP something we can trace?  Helper for supported_op. Read/Write
   flag in FLAG. Force state in S. Logs temporaries if LOG_TEMPS is true.
   Return true if traceable.  */

static bool
do_supported_op (tree op, vartrace_flags flag, attrstate s, bool log_temps)
{
  // cannot instrument accesses that throw because that would
  // need splitting BBs. (with some options they could be common?)
  if (tree_could_throw_p (op))
    return false;

  if (DECL_P (op))
    {
      // don't log temporaries, unless this is a return
      if ((DECL_IGNORED_P (op) || DECL_ARTIFICIAL (op)) && !log_temps)
	return false;
      if (s == force_on)
	return true;
      // rejects locals unless enabled.
      if (!locals_enabled(flag) && !DECL_EXTERNAL (op) && !TREE_STATIC (op))
	return false;
      if (flag_vartrace & flag)
	return true;
    }
  if (TREE_CODE (op) == SSA_NAME)
    {
      // don't log with computed gotos
      // would need code generation handling abnormal edges
      if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op))
	return false;
      if (log_temps && (flag_vartrace & flag))
	return true;

      // only log user visible variables for SSA_NAMES
      if (!SSA_NAME_VAR (op)
	  || DECL_ARTIFICIAL (SSA_NAME_VAR (op))
	  || DECL_IGNORED_P (SSA_NAME_VAR (op)))
	return false;

      if (s == force_on)
	return true;
      return locals_enabled(flag);
    }
  // log anything that references memory
  else if (TREE_CODE (op) == MEM_REF || TREE_CODE (op) == TARGET_MEM_REF)
    {
      // ??? Handle locals that use mem_ref here?
      if (s == force_on || (flag_vartrace & flag))
	return true;
    }

  return false;
}

/* Is OP something we can trace?  Read/Write flag in FLAG. Logs
   temporaries if LOG_TEMPS is true. Return true if traceable.  */

static bool
supported_op (tree op, vartrace_flags flag, bool log_temps)
{
  if (!supported_type (op))
    {
      log_op (op, "unsupported orig type");
      return false;
    }

  attrstate s = neutral;
  do
    {
      s = enabled_op (op, s);
      // also check the field declaration for attributes
      if (TREE_CODE (op) == COMPONENT_REF
	  || TREE_CODE (op) == BIT_FIELD_REF)
	s = enabled_op (TREE_OPERAND (op, 1), s);
    }
  while (handled_component_p (op) && (op = TREE_OPERAND (op, 0)));
  op = get_base_address (op);
  s = enabled_op (op, s);
  if (s == force_off)
    return false;

  if (do_supported_op (op, flag, s, log_temps))
    return true;

  log_op (op, "not tracing unsupported");
  return false;
}

/* Print debugging for inserting CODE at ORIG_STMT with type of VAL for WHY.  */

static void
log_trace_code (gimple *orig_stmt, gimple *code, tree val, const char *why)
{
  if (!dump_file)
    return;
  fprintf (dump_file, "%s:", IDENTIFIER_POINTER (DECL_NAME (current_function_decl)));
  if (orig_stmt)
    {
      location_t l = gimple_location (orig_stmt);
      if (l > BUILTINS_LOCATION)
	fprintf (dump_file, "%s:%d:%d:",
		 LOCATION_FILE (l), LOCATION_LINE (l), LOCATION_COLUMN (l));
      fprintf (dump_file, " BB%d ", gimple_bb (orig_stmt)->index);
    }
  fprintf (dump_file, "%s inserting ", why);
  print_gimple_stmt (dump_file, code, 0, TDF_VOPS);
  if (orig_stmt)
    {
      fprintf (dump_file, "orig ");
      print_gimple_stmt (dump_file, orig_stmt, 2, TDF_VOPS);
    }
  fprintf (dump_file, "type ");
  print_generic_expr (dump_file, TREE_TYPE (val), TDF_SLIM);
  fputc ('\n', dump_file);
  fputc ('\n', dump_file);
}

/* Insert STMT before or AFTER GI with location LOC.  */

static void insert_stmt(gimple_stmt_iterator *gi, gimple *stmt,
			location_t loc, bool after)
{
  gimple_set_location (stmt, loc);
  if (after)
    gsi_insert_after (gi, stmt, GSI_CONTINUE_LINKING);
  else
    gsi_insert_before (gi, stmt, GSI_SAME_STMT);
}

/* Insert conversion at GI from VAL to type TTYPE originally at LOC
   and STMT, inserting AFTER if after is true. Resulting value is
   returned.  */

static tree
insert_conversion (gimple_stmt_iterator *gi, tree val,
		   tree ttype,
		   gimple *stmt, location_t loc,
		   bool after)
{
  tree type = TREE_TYPE (val);
  if (ttype == type)
    return val;

  tree tvar = create_tmp_reg (ttype, "trace");
  if (ttype != type)
    {
      if ((INTEGRAL_TYPE_P (ttype) && INTEGRAL_TYPE_P (type))
		|| TYPE_SIZE_UNIT (ttype) != TYPE_SIZE_UNIT (type))
	val = build1 (CONVERT_EXPR, ttype, val);
      else
	val = build1 (VIEW_CONVERT_EXPR, ttype, val);
    }
  gassign *assign = gimple_build_assign (tvar, val);
  log_trace_code (stmt, assign, val, "conversion");
  insert_stmt (gi, assign, loc, after);
  return tvar;
}

/* Insert variable tracing code for VAL before iterator GI, originally
   for ORIG_STMT and optionally at LOC. Normally before ORIG_STMT, but
   AFTER if true. Reason is WHY. Return true if successfull.  */

static bool
insert_trace (gimple_stmt_iterator *gi, tree val, gimple *orig_stmt,
	      const char *why, location_t loc = -1, bool after = false)
{
  if (loc == (location_t)-1)
    loc = gimple_location (orig_stmt);

  tree type = TREE_TYPE (val);

  tree func = targetm.vartrace_func (TYPE_MODE (type), false);
  if (!func)
    return false;

  tree ttype = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (func)));

  val = unshare_expr (val);

  tree tvar = create_tmp_reg (type, "trace");
  gassign *assign = gimple_build_assign (tvar, val);
  log_trace_code (orig_stmt, assign, val, "mem access");
  insert_stmt (gi, assign, loc, after);

  tree cvar = insert_conversion (gi, tvar, ttype, orig_stmt, loc, after);

  gcall *call = gimple_build_call (func, 1, cvar);
  log_trace_code (NULL, call, cvar, why);
  insert_stmt (gi, call, loc, after);
  return true;
}

/* Try instrumenting ARG at position EGI in function FUN. Return true if
   changed.  */

static bool
try_instrument_arg (gimple_stmt_iterator *egi, tree arg, function *fun)
{
  if (!(flag_vartrace & VARTRACE_ARGS) && enabled_op (arg, neutral) != force_on)
    return false;

  if (TREE_CODE (arg) != PARM_DECL
      || DECL_BY_REFERENCE (arg) // arg will be pointer to arg. could reference?
      || DECL_IGNORED_P (arg)
      || !supported_type (arg))
    return false;

  tree sarg = ssa_default_def (fun, arg);
  if (sarg == NULL)
    return false;

  return insert_trace (egi, sarg, NULL, "arg",
		       fun->function_start_locus,
		       false);
}

/* Instrument arguments for FUN. Return true if changed.  */

static bool
instrument_args (function *fun)
{
  bool changed = false;
  gimple_stmt_iterator egi;

  // will be traced when read later. In theory we could check
  // if it is read unconditionally, but assume that the arguments
  // are not that interesting if they are not used.
  if (locals_enabled(VARTRACE_READS))
    return false;

  egi = gsi_after_labels (single_succ (ENTRY_BLOCK_PTR_FOR_FN (fun)));

  for (tree arg = DECL_ARGUMENTS (current_function_decl);
       arg != NULL_TREE;
       arg = DECL_CHAIN (arg))
    {
      if (try_instrument_arg (&egi, arg, fun))
	changed = true;
      else
	log_op (arg, "not tracing argument");
    }
  return changed;
}

/* Generate trace call for store ORIG at GI. Return true if
   successfull.  */

static bool
instrument_store (gimple_stmt_iterator *gi, gimple *stmt, tree orig)
{
  log_op_verbose (orig, "store");
  if (!supported_op (orig, VARTRACE_WRITES, false))
    return false;

  tree type = TREE_TYPE (orig);
  tree func = targetm.vartrace_func (TYPE_MODE (type), false);
  if (!func)
    return false;

  tree ttype = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (func)));
  location_t loc = gimple_location (stmt);

  /* Generate another reference to target. That can be racy, but is
     more likely to have the debug location of the target.  Better
     would be to use the original value to avoid any races, but we
     would need to somehow force the target location of the
     builtin.  */

  tree tvar = create_tmp_reg (type, "trace");
  gassign *assign = gimple_build_assign (tvar, orig);
  log_trace_code (stmt, assign, orig, "store copy");
  insert_stmt (gi, assign, loc, true);

  tvar = insert_conversion (gi, tvar, ttype, stmt, loc, true);

  gcall *tcall = gimple_build_call (func, 1, tvar);
  log_trace_code (stmt, tcall, tvar, "store");
  insert_stmt (gi, tcall, loc, true);
  return true;
}

/* Instrument return at statement STMT at GI needed. Return true if
   changed.  */

static bool
instrument_return (gimple_stmt_iterator *gi, greturn *gret)
{
  tree rval = gimple_return_retval (gret);

  if (!rval)
    return false;
  // check if it may be already traced to avoid redundancies
  // does not catch all cases
  if (TREE_CODE (rval) == SSA_NAME)
    {
      gimple *def_stmt = SSA_NAME_DEF_STMT (rval);
      if (gimple_code (def_stmt) == GIMPLE_ASSIGN
	  && gimple_assign_single_p (def_stmt)
	  && supported_op (gimple_assign_rhs1 (def_stmt),
			   (vartrace_flags)(VARTRACE_READS|VARTRACE_WRITES),
			   false))
	{
	  log_op (rval, "not tracing redundant return");
	  return false;
	}
    }
  // otherwise trace temporaries like expressions.
  if (supported_op (rval, VARTRACE_RETURNS, true))
    return insert_trace (gi, rval, gret, "return");
  log_op (rval, "not tracing unsupported return");
  return false;
}

/* Data for the loads/stores walker.  */

struct visit_data {
  gimple_stmt_iterator *gi;
};

/* Visit all loads at STMT with tree OP and visit_data DATA.  */

static bool
vartrace_visit_load (gimple *stmt, tree, tree op, void *data)
{
  visit_data *vd = (visit_data *)data;
  bool log_temps = false;

  log_op_verbose (op, "load op");
  return supported_op (op, VARTRACE_READS, log_temps)
    && insert_trace (vd->gi, op, stmt, "stmt mem load", -1, true);
}

/* Visit all stores at STMT with tree OP and visit_data DATA.  */

static bool
vartrace_visit_store (gimple *stmt, tree, tree op, void *data)
{
  visit_data *vd = (visit_data *)data;

  log_op_verbose (op, "store op");
  return supported_op (op, VARTRACE_WRITES, false)
    && insert_trace (vd->gi, op, stmt, "stmt mem store", -1, true);
}

/* Insert vartrace calls for FUN.  */

static unsigned int
vartrace_execute (function *fun)
{
  basic_block bb;
  gimple_stmt_iterator gi;
  vartrace_flags old_state = (vartrace_flags)flag_vartrace;

  if (lookup_attribute ("vartrace", TYPE_ATTRIBUTES (TREE_TYPE (fun->decl)))
      || lookup_attribute ("vartrace", DECL_ATTRIBUTES (fun->decl)))
    {
      // reset the global temporarily for a forced function
      flag_vartrace = VARTRACE_READS | VARTRACE_WRITES | VARTRACE_ARGS
	| VARTRACE_RETURNS;
    }

  bool changed = instrument_args (fun);

  /* Instrument each gimple statement.  */
  FOR_EACH_BB_FN (bb, fun)
    for (gi = gsi_start_bb (bb); !gsi_end_p (gi); gsi_next (&gi))
      {
	gimple *stmt = gsi_stmt (gi);

	if (gimple_code (stmt) == GIMPLE_RETURN)
	  {
	    changed |= instrument_return (&gi, as_a<greturn *> (stmt));
	    continue;
	  }

	/* Cannot handle asm goto. Ignore for now.  */
	if (gimple_code (stmt) == GIMPLE_ASM
		&& gimple_asm_nlabels (as_a<gasm *> (stmt)) > 0)
	  continue;

	/* Instrument DEFs.  */
	def_operand_p defp;
	ssa_op_iter iter;
	FOR_EACH_SSA_DEF_OPERAND (defp, stmt, iter, SSA_OP_DEF)
	  {
	    tree def = DEF_FROM_PTR (defp);
	    changed |= instrument_store (&gi, stmt, def);
	  }

	// For locals tracing need to handle uses, otherwise
	// very little is traced with -O2
	if (flag_vartrace & VARTRACE_LOCALS)
	  {
	    use_operand_p usep;

	    FOR_EACH_SSA_USE_OPERAND (usep, stmt, iter, SSA_OP_USE)
	      {
		tree use = USE_FROM_PTR (usep);
		log_op_verbose (use, "local use");
		if (supported_op (use, VARTRACE_READS, false))
		  changed |= insert_trace (&gi, use, stmt, "local use");
	      }
	  }

	/* And memory loads and stores.  */
	visit_data vd;
	vd.gi = &gi;
	changed |= walk_stmt_load_store_ops (stmt, &vd, vartrace_visit_load,
					     vartrace_visit_store);
      }

  flag_vartrace = old_state;

  // for now, until we fix all cases that destroy ssa
  return changed ? TODO_update_ssa : 0;;
}

static const pass_data pass_data_vartrace =
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
      if (flag_vartrace & VARTRACE_OFF)
	return false;

      // check if vartrace is supported in backend
      if (!targetm.vartrace_func
	  || targetm.vartrace_func (SImode, false) == NULL)
	return false;

      // disabled for function?
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
