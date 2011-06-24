/* This file is part of GCC.

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
#include "ansidecl.h"
#include "coretypes.h"
#include "tm.h"
#include "opts.h"
#include "tree.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "gimple.h"
#include "toplev.h"
#include "debug.h"
#include "options.h"
#include "flags.h"
#include "convert.h"
#include "diagnostic-core.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "target.h"
#include "cgraph.h"

#include <gmp.h>
#include <mpfr.h>

#include "vec.h"
#include "hashtab.h"

#include "gpython.h"
#include "py-dot-codes.def"
#include "py-dot.h"
#include "py-vec.h"
#include "py-tree.h"
#include "py-types.h"
#include "py-runtime.h"

static VEC(tree,gc) * gpy_stmt_pass_lower_genericify (gpy_hash_tab_t *, VEC(gpydot,gc) *);
static tree gpy_stmt_pass_lower_get_module_type (const char *, gpy_hash_tab *);
static void gpy_stmt_pass_lower_gen_toplevl_context (tree, gpy_hash_tab_t *);

static
tree gpy_stmt_pass_lower_get_module_type (const char * s, 
					  gpy_hash_tab_t * modules)
{
  tree retval = error_mark_node;

  debug ("looking for module type <%s>!\n", s);
  gpy_hashval_t h = gpy_dd_hash_string (s);
  gpy_hash_entry_t * e = gpy_dd_hash_lookup_table (modules, h);
  if (e->data)
    e = (tree) e->data;

  return retval;
}

static
void gpy_stmt_pass_lower_gen_toplevl_context (tree module, tree param_decl,
					      gpy_hash_tab_t * context)
{
  if (module == error_mark_node)
    return;
  else
    {
      gcc_assert (TREE_CODE (module) == TYPE_DECL);
      tree field = TYPE_FIELDS (module);

      do {
	gcc_assert (TREE_CODE (field) == FIELD_DECL);

	gpy_hashval_t h = gpy_dd_hash_string (IDENTIFIER_POINTER(DECL_NAME (field)));
	tree ref = build3 (COMPONENT_REF, TREE_TYPE (field), param_decl, field);
	void ** e = gpy_dd_hash_insert (h, ref, context);

	if (e)
	  fatal_error ("problem inserting component reference into context!\n");
      } while (field = DECL_CHAIN (field));
    }
}

/* 
   Creates a DECL_CHAIN of stmts to fold the scalar 
   with the last tree being the address of the primitive 
*/
tree gpy_stmt_decl_lower_scalar (gpy_dot_tree  * decl)
{
  tree retval = error_mark_node;

  gcc_assert (DOT_TYPE (decl) == D_PRIMITIVE);
  gcc_assert (DOT_lhs_T (decl) == D_TD_COM);

  switch (DOT_lhs_TC (decl).T)
    {
    case D_T_INTEGER:
      {
	tree address = build_decl (UNKNOWN_LOCATION, VAR_DECL,
				   create_tmp_var_name ("P"),
				   gpy_object_type_ptr);
	retval = build2 (MODIFY_EXPR, gpy_object_type_ptr, address,
			 gpy_builtin_get_fold_int_call (DOT_lhs_TC (decl).o.integer));
	DECL_CHAIN (retval) = address;
      }
      break;

    default:
      error ("invalid scalar type!\n");
      break;
    }

  return retval;
}

tree gpy_stmt_decl_lower_modify (gpy_dot_tree_t * decl,
				 VEC(gpy_ctx_t,gc) * context)
{
  tree lhs = DOT_lhs_TT (decl);
  tree rhs = DOT_rhs_TT (decl);

  /*
    We dont handle full target lists yet
    all targets are in the lhs tree.
   
    To implment a target list such as:
    x,y,z = 1

    The lhs should be a DOT_CHAIN of identifiers!
    So we just iterate over them and deal with it as such!
   */

  gcc_assert (DOT_TYPE (lhs) == D_IDENTIFIER);

  tree addr = gpy_ctx_lookup_decl (context, DOT_IDENTIFIER_POINTER (lhs));
  if (!addr)
    {
      /* since not previously declared we need to declare the variable! */
      gpy_hash_tab_t * current_context = VEC_index (gpy_ctx_t, context,
						    (VEC_length (gpy_ctx_t, context)
						     - 1)
						    );
      addr = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			 get_identifier (DOT_IDENTIFIER_POINTER (lhs)),
			 gpy_object_type_ptr);

      if (!gpy_ctx_push_decl (addr, DOT_IDENTIFIER_POINTER (lhs), 
			      current_context))
	fatal_error ("error pushing decl <%q+E>!\n", IDENTIFIER_POINTER (addr));
    }
  gcc_assert (addr != error_mark_node);

  tree evaluated_rhs_tree  = gpy_stmt_decl_lower_expr (rhs, context);

  
}

tree gpy_stmt_decl_lower_addition (gpy_dot_tree_t * decl,
				   VEC(gpy_ctx_t,gc) * context)
{
  
}

tree gpy_stmt_decl_lower_expr (gpy_dot_tree_t * decl,
			       VEC(gpy_ctx_t,gc) * context)
{
  tree stmt_list = NULL_TREE;

  switch (DOT_TYPE (decl))
    {
    case D_PRIMITIVE:
      stmt_list = gpy_stmt_decl_lower_scalar (decl);
      break;

    case D_IDENTIFIER:
      stmt_list = gpy_ctx_lookup_decl (context,
				       DOT_IDENTIFIER_POINTER (decl));
      break;

    default:
      {
	switch (DOT_TYPE (decl))
	  {
	  case D_MODIFY_EXPR:
	    stmt_list = gpy_stmt_decl_lower_modify (decl, context);
	    break;

	  case D_ADD_EXPR:
	    stmt_list = gpy_stmt_decl_lower_addition (decl, context);
	    break;

	    // ... the rest of the binary operators!

	  default:
	    {
	      error ("unhandled operation type!\n");
	      stmt_list = error_mark_node;
	    }
	    break;
	  }
      }
      break;
    }

  return stmt_list;
}

tree gpy_stmt_pass_lower_functor (gpy_dot_tree_t * decl,
				  gpy_hash_tab_t * modules)
{
  return error_mark_node;
}

static
VEC(tree,gc) * gpy_stmt_pass_lower_genericify (gpy_hash_tab_t * modules,
					       VEC(gpydot,gc) * decls)
{
  VEC(tree,gc) * retval = VEC_alloc (tree,gc,0);
  
  tree block = alloc_stmt_list ();
  tree declvars = NULL_TREE;

  gpy_hash_tab_t toplvl, topnxt;
  gpy_dd_hash_init_table (&toplvl);
  gpy_dd_hash_init_table (&topnxt);

  tree main_init_module = gpy_stmt_pass_lower_get_module_type ("main.main",
							       modules);
  
  tree main_init_fntype = build_function_type_list (build_pointer_type (main_init_module),
						    NULL_TREE);
  tree main_init_fndecl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
				      get_identifier ("main.main.init"),
				      main_init_fntype);
  DECL_EXTERNAL (main_init_fndecl) = 0;
  TREE_PUBLIC (main_init_fndecl) = 1;
  TREE_STATIC (main_init_fndecl) = 1;

  tree self_parm_decl = build_decl (BUILTINS_LOCATION, PARM_DECL,
				    get_identifier ("__self__"),
				    TYPE_ARG_TYPES (TREE_TYPE (fndecl))
				    );
  DECL_CONTEXT (self_parm_decl) = fndecl;
  DECL_ARG_TYPE (self_parm_decl) = TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (fndecl)));
  TREE_READONLY (self_parm_decl) = 1;

  gpy_stmt_pass_lower_gen_toplevl_context (main_init_module, self_parm_decl,
					   &toplvl);

  VEC(gpy_ctx_t,gc) * toplevl_context = VEC_alloc (gpy_ctx_t, 0);
  VEC_safe_push (gpy_ctx_t, gc, toplevl_context, toplvl);
  VEC_safe_push (gpy_ctx_t, gc, toplevl_context, topnxt);
  
  int idx = 0;
  gpy_dot_tree_t * idtx = NULL_DOT;
  /*
    Iterating over the DOT IL to lower/generate the GENERIC code
    required to compile the stmts and decls
   */
  for (; VEC_iterate (gpydot, decls, idx, idtx); ++idx)
    {
      if (DOT_FIELD (idtx) ==  D_D_EXPR)
	{
	  // append to stmt list as this goes into the module initilizer...
	  append_to_statement_list (gpy_stmt_decl_lower_expr (idtx,
							      toplevl_context
							      ),
				    &block);
	  continue;
	}

      switch (DOT_TYPE (idtx))
	{
	case D_STRUCT_METHOD:
	  {
	    /* 
	       They are self contained decls so we just pass them to the 
	       return vector for gimplification
	    */
	    VEC_safe_push (tree, gc, retval, 
			   gpy_stmt_pass_lower_functor (idtx, modules)
			   );
	  }
	  break;

	default:
	  fatal_error ("unhandled dot tree code <%i>!\n", DOT_TYPE (idtx));
	  break;
	}
    }
  

  return retval;
}

/* Now we start to iterate over the dot IL to lower it down to */
/* a vector of GENERIC decls */

/* Which is then passed over to the pass manager for any other */
/* defined passes which can be placed into the queue but arent */
/* nessecary for now. */
VEC(tree,gc) * gpy_stmt_pass_lower (VEC(tree,gc) *modules,
				    VEC(gpydot,gc) *decls)
{
  VEC(tree,gc) * retval;

  gpy_hash_tab_t module_ctx;
  gpy_dd_hash_init_table (&module_ctx);

  int idx = 0;
  gpy_dot_tree_t * idtx = NULL_DOT;
  tree itx = NULL_TREE;

  for (; VEC_iterate (tree, modules, idx, itx); ++idx)
    {
      gcc_assert (TREE_CODE (itx) == TYPE_DECL);

      debug ("hashing module name <%s>!\n", IDENTIFIER_POINTER (DECL_NAME(itx)));
      gpy_hashval_t h = gpy_dd_hash_string (IDENTIFIER_POINTER (DECL_NAME(itx)));
      void ** e = gpy_dd_hash_insert (h, itx, &module_ctx);

      if (e)
	fatal_error ("module <%q+E> is already defined!\n", itx);
    }

  retval =  gpy_stmt_pass_lower_genericify (&module_ctx, decls);
  VEC_safe_push (tree, gc, retval,
		 gpy_stmt_pass_lower_gen_main (gpy_stmt_pass_lower_get_module_type ("main.main",
										    module_ctx)
					       )
		 );
  // free (context.array);

  return retval;
}
