/* Language-independent diagnostic subroutines for the GNU C compiler
   Copyright (C) 1999, 2000 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */


/* This file implements the language independant aspect of diagnostic
   message module.  */

#include "config.h"
#undef FLOAT /* This is for hpux. They should change hpux.  */
#undef FFS  /* Some systems define this in param.h.  */
#include "system.h"

#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "flags.h"
#include "input.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "insn-config.h"
#include "toplev.h"
#include "intl.h"
#include "diagnostic.h"

#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free  free

#define diagnostic_args diagnostic_buffer->format_args
#define diagnostic_msg diagnostic_buffer->cursor

#define output_formatted_integer(BUFFER, FORMAT, INTEGER) \
  do {                                                    \
    sprintf (digit_buffer, FORMAT, INTEGER);              \
    output_add_string (BUFFER, digit_buffer);             \
  } while (0)

/* This data structure serves to save/restore an output_buffer state.  */
typedef struct
{
  const char *prefix;
  int maximum_length;
  int ideal_maximum_length;
  int emitted_prefix_p;
  int prefixing_rule;
  const char *cursor;
  va_list format_args;
} output_state;


/* Prototypes. */
static int doing_line_wrapping PARAMS ((void));

static void finish_diagnostic PARAMS ((void));
static void output_do_verbatim PARAMS ((output_buffer *,
                                        const char *, va_list));
static void output_to_stream PARAMS ((output_buffer *, FILE *));
static void output_format PARAMS ((output_buffer *));

static char *vbuild_message_string PARAMS ((const char *, va_list));
static char *build_message_string PARAMS ((const char *, ...))
     ATTRIBUTE_PRINTF_1;
static char *context_as_prefix PARAMS ((const char *, int, int));
static void output_do_printf PARAMS ((output_buffer *, const char *));
static void line_wrapper_printf PARAMS ((FILE *, const char *, ...))
     ATTRIBUTE_PRINTF_2;
static void vline_wrapper_message_with_location PARAMS ((const char *, int,
							 int, const char *,
							 va_list));
static void notice PARAMS ((const char *s, ...)) ATTRIBUTE_PRINTF_1;
static void v_message_with_file_and_line PARAMS ((const char *, int, int,
						  const char *, va_list));
static void v_message_with_decl PARAMS ((tree, int, const char *, va_list));
static void file_and_line_for_asm PARAMS ((rtx, const char **, int *));
static void v_error_with_file_and_line PARAMS ((const char *, int,
						const char *, va_list));
static void v_error_with_decl PARAMS ((tree, const char *, va_list));
static void v_error_for_asm PARAMS ((rtx, const char *, va_list));
static void vfatal PARAMS ((const char *, va_list)) ATTRIBUTE_NORETURN;
static void v_warning_with_file_and_line PARAMS ((const char *, int,
						  const char *, va_list));
static void v_warning_with_decl PARAMS ((tree, const char *, va_list));
static void v_warning_for_asm PARAMS ((rtx, const char *, va_list));
static void v_pedwarn_with_decl PARAMS ((tree, const char *, va_list));
static void v_pedwarn_with_file_and_line PARAMS ((const char *, int,
						  const char *, va_list));
static void vsorry PARAMS ((const char *, va_list));
static void report_file_and_line PARAMS ((const char *, int, int));
static void vnotice PARAMS ((FILE *, const char *, va_list));
static void set_real_maximum_length PARAMS ((output_buffer *));

static void save_output_state PARAMS ((const output_buffer *, output_state *));
static void restore_output_state PARAMS ((const output_state *,
                                          output_buffer *));
                                          
static void output_unsigned_decimal PARAMS ((output_buffer *, unsigned int));
static void output_long_decimal PARAMS ((output_buffer *, long int));
static void output_long_unsigned_decimal PARAMS ((output_buffer *,
                                                  long unsigned int));
static void output_octal PARAMS ((output_buffer *, int));
static void output_long_octal PARAMS ((output_buffer *, long int));
static void output_hexadecimal PARAMS ((output_buffer *, int));
static void output_long_hexadecimal PARAMS ((output_buffer *, long int));
static void output_append_r PARAMS ((output_buffer *, const char *, int));
static void wrap_text PARAMS ((output_buffer *, const char *, const char *));
static void maybe_wrap_text PARAMS ((output_buffer *, const char *,
                                     const char *));

extern int rtl_dump_and_exit;
extern int inhibit_warnings;
extern int warnings_are_errors;
extern int warningcount;
extern int errorcount;

/* Front-end specific tree formatter, if non-NULL.  */
printer_fn lang_printer = NULL;

/* This must be large enough to hold any printed integer or
   floating-point value.  */
static char digit_buffer[128];

/* An output_buffer surrogate for stderr.  */
static output_buffer global_output_buffer;
output_buffer *diagnostic_buffer = &global_output_buffer;

static int need_error_newline;

/* Function of last error message;
   more generally, function such that if next error message is in it
   then we don't have to mention the function name.  */
static tree last_error_function = NULL;

/* Used to detect when input_file_stack has changed since last described.  */
static int last_error_tick;

/* Called by report_error_function to print out function name.
 * Default may be overridden by language front-ends.  */

void (*print_error_function) PARAMS ((const char *)) =
  default_print_error_function;

/* Maximum characters per line in automatic line wrapping mode.
   Zero means don't wrap lines. */

int diagnostic_message_length_per_line;

/* Used to control every diagnostic message formatting.  Front-ends should
   call set_message_prefixing_rule to set up their politics.  */
static int current_prefixing_rule;

/* Initialize the diagnostic message outputting machinery.  */

void
initialize_diagnostics ()
{
  /* By default, we don't line-wrap messages.  */
  diagnostic_message_length_per_line = 0;
  set_message_prefixing_rule (DIAGNOSTICS_SHOW_PREFIX_ONCE);
  /* Proceed to actual initialization.  */
  default_initialize_buffer (diagnostic_buffer);
}

/* Predicate. Return 1 if we're in automatic line wrapping mode.  */

static int
doing_line_wrapping ()
{
  return diagnostic_message_length_per_line > 0;
}

void
set_message_prefixing_rule (rule)
     int rule;
{
  current_prefixing_rule = rule;
}

/* Returns true if BUFFER is in line-wrappind mode.  */
int
output_is_line_wrapping (buffer)
     output_buffer *buffer;
{
  return buffer->ideal_maximum_length > 0;
}

/* Return BUFFER's prefix.  */
const char *
output_get_prefix (buffer)
     const output_buffer *buffer;
{
  return buffer->prefix;
}

/* Subroutine of output_set_maximum_length.  Set up BUFFER's
   internal maximum characters per line.  */
static void
set_real_maximum_length (buffer)
     output_buffer *buffer;
{
  /* If we're told not to wrap lines then do the obvious thing.  */
  if (! output_is_line_wrapping (buffer))
    buffer->maximum_length = buffer->ideal_maximum_length;
  else
    {
      int prefix_length = buffer->prefix ? strlen (buffer->prefix) : 0;
      /* If the prefix is ridiculously too long, output at least
         32 characters.  */
      if (buffer->ideal_maximum_length - prefix_length < 32)
        buffer->maximum_length = buffer->ideal_maximum_length + 32;
      else
        buffer->maximum_length = buffer->ideal_maximum_length;
    }
}

/* Sets the number of maximum characters per line BUFFER can output
   in line-wrapping mode.  A LENGTH value 0 suppresses line-wrapping.  */
void
output_set_maximum_length (buffer, length)
     output_buffer *buffer;
     int length;
{
  buffer->ideal_maximum_length = length;
  set_real_maximum_length (buffer);
}

/* Sets BUFFER's PREFIX.  */
void
output_set_prefix (buffer, prefix)
     output_buffer *buffer;
     const char *prefix;
{
  buffer->prefix = prefix;
  set_real_maximum_length (buffer);
  buffer->emitted_prefix_p = 0;
}

/* Free BUFFER's prefix, a previously malloc()'d string.  */

void
output_destroy_prefix (buffer)
     output_buffer *buffer;
{
  if (buffer->prefix)
    {
      free ((char *) buffer->prefix);
      buffer->prefix = NULL;
    }
}

/* Construct an output BUFFER with PREFIX and of MAXIMUM_LENGTH
   characters per line.  */
void
init_output_buffer (buffer, prefix, maximum_length)
     output_buffer *buffer;
     const char *prefix;
     int maximum_length;
{
  obstack_init (&buffer->obstack);
  buffer->ideal_maximum_length = maximum_length;
  buffer->line_length = 0;
  output_set_prefix (buffer, prefix);
  buffer->emitted_prefix_p = 0;
  buffer->prefixing_rule = current_prefixing_rule;
  
  buffer->cursor = NULL;
}

/* Initialize BUFFER with a NULL prefix and current diagnostic message
   length cutoff.  */
void
default_initialize_buffer (buffer)
     output_buffer *buffer;
{
  init_output_buffer (buffer, NULL, diagnostic_message_length_per_line);
}

/* Recompute diagnostic_buffer's attributes to reflect any change
   in diagnostic formatting global options.  */
void
reshape_diagnostic_buffer ()
{
  diagnostic_buffer->ideal_maximum_length = diagnostic_message_length_per_line;
  diagnostic_buffer->prefixing_rule = current_prefixing_rule;
  set_real_maximum_length (diagnostic_buffer);
}

/* Reinitialize BUFFER.  */
void
output_clear (buffer)
     output_buffer *buffer;
{
  obstack_free (&buffer->obstack, obstack_base (&buffer->obstack));
  buffer->line_length = 0;
  buffer->cursor = NULL;
  buffer->emitted_prefix_p = 0;
}

/* Finishes to construct a NULL-terminated character string representing
   the BUFFERed message.  */
const char *
output_finish (buffer)
     output_buffer *buffer;
{
  obstack_1grow (&buffer->obstack, '\0');
  return (const char *) obstack_finish (&buffer->obstack);
}

/* Return the amount of characters BUFFER can accept to
   make a full line.  */
int
output_space_left (buffer)
     const output_buffer *buffer;
{
  return buffer->maximum_length - buffer->line_length;
}

/* Write out BUFFER's prefix.  */
void
output_emit_prefix (buffer)
     output_buffer *buffer;
{
  if (buffer->prefix)
    {
      switch (buffer->prefixing_rule)
        {
        default:
        case DIAGNOSTICS_SHOW_PREFIX_NEVER:
          break;

        case DIAGNOSTICS_SHOW_PREFIX_ONCE:
          if (buffer->emitted_prefix_p)
            break;
          else
            buffer->emitted_prefix_p = 1;
          /* Fall through.  */

        case DIAGNOSTICS_SHOW_PREFIX_EVERY_LINE:
          buffer->line_length += strlen (buffer->prefix);
          obstack_grow
            (&buffer->obstack, buffer->prefix, buffer->line_length);
          break;
        }
    }
}

/* Have BUFFER start a new line.  */
void
output_add_newline (buffer)
     output_buffer *buffer;
{
  obstack_1grow (&buffer->obstack, '\n');
  buffer->line_length = 0;
}

/* Appends a character to BUFFER.  */
void
output_add_character (buffer, c)
     output_buffer *buffer;
     int c;
{
  if (output_is_line_wrapping (buffer) && output_space_left (buffer) <= 0)
    output_add_newline (buffer);
  obstack_1grow (&buffer->obstack, c);
  ++buffer->line_length;
}

/* Adds a space to BUFFER.  */
void
output_add_space (buffer)
     output_buffer *buffer;
{
  if (output_is_line_wrapping (buffer) && output_space_left (buffer) <= 0)
    {
      output_add_newline (buffer);
      return;
    }
  obstack_1grow (&buffer->obstack, ' ');
  ++buffer->line_length;
}

/* These functions format an INTEGER into BUFFER as suggested by their
   names.  */
void
output_decimal (buffer, i)
     output_buffer *buffer;
     int i;
{
  output_formatted_integer (buffer, "%d", i);
}

static void
output_long_decimal (buffer, i)
     output_buffer *buffer;
     long int i;
{
  output_formatted_integer (buffer, "%ld", i);
}

static void
output_unsigned_decimal (buffer, i)
     output_buffer *buffer;
     unsigned int i;
{
  output_formatted_integer (buffer, "%u", i);
}

static void
output_long_unsigned_decimal (buffer, i)
     output_buffer *buffer;
     long unsigned int i;
{
  output_formatted_integer (buffer, "%lu", i);
}

static void
output_octal (buffer, i)
     output_buffer *buffer;
     int i;
{
  output_formatted_integer (buffer, "%o", i);
}

static void
output_long_octal (buffer, i)
     output_buffer *buffer;
     long int i;
{
  output_formatted_integer (buffer, "%lo", i);
}

static void
output_hexadecimal (buffer, i)
     output_buffer *buffer;
     int i;
{
  output_formatted_integer (buffer, "%x", i);
}

static void
output_long_hexadecimal (buffer, i)
     output_buffer *buffer;
     long int i;
{
  output_formatted_integer (buffer, "%lx", i);
}

/* Append to BUFFER a string specified by its STARTING character
   and LENGTH.  */
static void
output_append_r (buffer, start, length)
     output_buffer *buffer;
     const char *start;
     int length;
{
  obstack_grow (&buffer->obstack, start, length);
  buffer->line_length += length;
}

/* Append a string deliminated by START and END to BUFFER.  No wrapping is
   done.  However, if beginning a new line then emit BUFFER->PREFIX and
   skip any leading whitespace if appropriate.  The caller must ensure
   that it is safe to do so.  */
void
output_append (buffer, start, end)
     output_buffer *buffer;
     const char *start;
     const char *end;
{
  /* Emit prefix and skip whitespace if we're starting a new line.  */
  if (buffer->line_length == 0)
    {
      output_emit_prefix (buffer);
      if (output_is_line_wrapping (buffer))
        while (start != end && *start == ' ')
          ++start;
    }
  output_append_r (buffer, start, end - start);
}

/* Wrap a text delimited by START and END into BUFFER.  */
static void
wrap_text (buffer, start, end)
     output_buffer *buffer;
     const char *start;
     const char *end;
{
  while (start != end)
    {
      /* Dump anything bodered by whitespaces.  */ 
      {
        const char *p = start;
        while (p != end && *p != ' ' && *p != '\n')
          ++p;
        if (p - start >= output_space_left (buffer))
          output_add_newline (buffer);
        output_append (buffer, start, p);
        start = p;
      }

      if (start != end && *start == ' ')
        {
          output_add_space (buffer);
          ++start;
        }
      if (start != end && *start == '\n')
        {
          output_add_newline (buffer);
          ++start;
        }
    }
}

/* Same as wrap_text but wrap text only when in line-wrapping mode.  */
static void
maybe_wrap_text (buffer, start, end)
     output_buffer *buffer;
     const char *start;
     const char *end;
{
  if (output_is_line_wrapping (buffer))
    wrap_text (buffer, start, end);
  else
    output_append (buffer, start, end);
}


/* Append a STRING to BUFFER; the STRING maybe be line-wrapped if in
   appropriate mode.  */

void
output_add_string (buffer, str)
     output_buffer *buffer;
     const char *str;
{
  maybe_wrap_text (buffer, str, str + (str ? strlen (str) : 0));
}

/* Flush the content of BUFFER onto FILE and reinitialize BUFFER.  */

static void
output_to_stream (buffer, file)
     output_buffer *buffer;
     FILE *file;
{
  const char *text = output_finish (buffer);
  fputs (text, file);
  output_clear (buffer);
}

/* Format a message pointed to by BUFFER->CURSOR using BUFFER->CURSOR
   as appropriate.  The following format specifiers are recognized as
   being language independent:
   %d, %i: (signed) integer in base ten.
   %u: unsigned integer in base ten.
   %o: (signed) integer in base eight.
   %x: (signged) integer in base sixteen.
   %ld, %li, %lo, %lu, %lx: long versions of the above.
   %c: character.
   %s: string.
   %%: `%'.
   %*.s: a substring the length of which is specified by an integer.  */
static void
output_format (buffer)
     output_buffer *buffer;
{
  const char *msg = buffer->cursor;
  for (; *buffer->cursor; ++buffer->cursor)
    {
      int long_integer = 0;
      /* Ignore text.  */
      {
        const char *p = buffer->cursor;
        while (*p && *p != '%')
          ++p;
        maybe_wrap_text (buffer, buffer->cursor, p);
        buffer->cursor = p;
      }
      if (!*buffer->cursor)
        break;

      /* We got a '%'.  Let's see what happens. Record whether we're
         parsing a long integer format specifier.  */
      if (*++buffer->cursor == 'l')
        {
          long_integer = 1;
          ++buffer->cursor;
        }

      /* Handle %c, %d, %i, %ld, %li, %lo, %lu, %lx, %o, %s, %u,
         %x, %.*s; %%.  And nothing else.  Front-ends should install
         printers to grok language specific format specifiers.  */
      switch (*buffer->cursor)
        {
        case 'c':
          output_add_character
            (buffer, va_arg (buffer->format_args, int));
          break;
          
        case 'd':
        case 'i':
          if (long_integer)
            output_long_decimal
              (buffer, va_arg (buffer->format_args, long int));
          else
            output_decimal (buffer, va_arg (buffer->format_args, int));
          break;

        case 'o':
          if (long_integer)
            output_long_octal
              (buffer, va_arg (buffer->format_args, long int));
          else
            output_octal (buffer, va_arg (buffer->format_args, int));
          break;

        case 's':
          output_add_string
            (buffer, va_arg (buffer->format_args, const char *));
          break;

        case 'u':
          if (long_integer)
            output_long_unsigned_decimal
              (buffer, va_arg (buffer->format_args, long unsigned int));
          else
            output_unsigned_decimal
              (buffer, va_arg (buffer->format_args, unsigned int));
          
        case 'x':
          if (long_integer)
            output_long_hexadecimal
              (buffer, va_arg (buffer->format_args, long int));
          else
            output_hexadecimal (buffer, va_arg (buffer->format_args, int));
          break;

        case '%':
          output_add_character (buffer, '%');
          break;

        case '.':
          {
            int n;
            /* We handle no precision specifier but `%.*s'.  */
            if (*++buffer->cursor != '*')
              abort ();
            else if (*++buffer->cursor != 's')
              abort();
            n = va_arg (buffer->format_args, int);
            output_append (buffer, msg, msg + n);
          }
          break;

        default:
          if (!lang_printer || !(*lang_printer) (buffer))
            {
              /* Hmmm.  The front-end failed to install a format translator
                 but called us with an unrecognized format.  Sorry.  */
              abort();
            }
        }
    }
}

static char *
vbuild_message_string (msgid, ap)
     const char *msgid;
     va_list ap;
{
  char *str;

  vasprintf (&str, msgid, ap);
  return str;
}

/*  Return a malloc'd string containing MSGID formatted a la
    printf.  The caller is reponsible for freeing the memory.  */

static char *
build_message_string VPARAMS ((const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *msgid;
#endif
  va_list ap;
  char *str;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  msgid = va_arg (ap, const char *);
#endif

  str = vbuild_message_string (msgid, ap);

  va_end (ap);

  return str;
}


/* Return a malloc'd string describing a location.  The caller is
   responsible for freeing the memory.  */

static char *
context_as_prefix (file, line, warn)
     const char *file;
     int line;
     int warn;
{
  if (file)
    {
      if (warn)
	return build_message_string ("%s:%d: warning: ", file, line);
      else
	return build_message_string ("%s:%d: ", file, line);
    }
  else
    {
      if (warn)
	return build_message_string ("%s: warning: ", progname);
      else
	return build_message_string ("%s: ", progname);
    }
}

/* Format a MESSAGE into BUFFER.  Automatically wrap lines.  */

static void
output_do_printf (buffer, msgid)
     output_buffer *buffer;
     const char *msgid;
{
  char *message = vbuild_message_string (msgid, buffer->format_args);

  output_add_string (buffer, message);
  free (message);
}


/* Format a message into BUFFER a la printf.  */

void
output_printf VPARAMS ((struct output_buffer *buffer, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  struct output_buffer *buffer;
  const char *msgid;
#endif
  va_list ap;
  va_list old_args;

  VA_START (ap, msgid);
#ifndef ANSI_PROTOTYPES
  buffer = va_arg (ap, struct output_buffer *);
  msgid = va_arg (ap, const char *);
#endif
  va_copy (old_args, buffer->format_args);

  va_copy (buffer->format_args, ap);
  output_do_printf (buffer, msgid);
  va_end (buffer->format_args);

  va_copy (buffer->format_args, old_args);
}


/* Format a MESSAGE into FILE.  Do line wrapping, starting new lines
   with PREFIX.  */

static void
line_wrapper_printf VPARAMS ((FILE *file, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  FILE *file;
  const char *msgid;
#endif
  output_buffer buffer;
  
  default_initialize_buffer (&buffer);
  VA_START (buffer.format_args, msgid);

#ifndef ANSI_PROTOTYPES
  file = va_arg (buffer.format_args, FILE *);
  msgid = va_arg (buffer.format_args, const char *);
#endif  

  output_do_printf (&buffer, msgid);
  output_to_stream (&buffer, file);

  va_end (buffer.format_args);
}


static void
vline_wrapper_message_with_location (file, line, warn, msgid, ap)
     const char *file;
     int line;
     int warn;
     const char *msgid;
     va_list ap;
{
  output_buffer buffer;
  
  init_output_buffer (&buffer, context_as_prefix (file, line, warn),
		      diagnostic_message_length_per_line);
  va_copy (buffer.format_args, ap);
  output_do_printf (&buffer, msgid);
  output_to_stream (&buffer, stderr);

  output_destroy_prefix (&buffer);
  fputc ('\n', stderr);
}


/* Print the message MSGID in FILE.  */

static void
vnotice (file, msgid, ap)
     FILE *file;
     const char *msgid;
     va_list ap;
{
  vfprintf (file, _(msgid), ap);
}

/* Print MSGID on stderr.  */

static void
notice VPARAMS ((const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  msgid = va_arg (ap, char *);
#endif

  vnotice (stderr, msgid, ap);
  va_end (ap);
}

/* Report FILE and LINE (or program name), and optionally just WARN.  */

static void
report_file_and_line (file, line, warn)
     const char *file;
     int line;
     int warn;
{
  if (file)
    fprintf (stderr, "%s:%d: ", file, line);
  else
    fprintf (stderr, "%s: ", progname);

  if (warn)
    notice ("warning: ");
}

/* Print a message relevant to line LINE of file FILE.  */

static void
v_message_with_file_and_line (file, line, warn, msgid, ap)
     const char *file;
     int line;
     int warn;
     const char *msgid;
     va_list ap;
{
  report_file_and_line (file, line, warn);
  vnotice (stderr, msgid, ap);
  fputc ('\n', stderr);
}

/* Print a message relevant to the given DECL.  */

static void
v_message_with_decl (decl, warn, msgid, ap)
     tree decl;
     int warn;
     const char *msgid;
     va_list ap;
{
  const char *p;
  output_buffer buffer;

  if (doing_line_wrapping ())
    {
      init_output_buffer
        (&buffer, context_as_prefix
         (DECL_SOURCE_FILE (decl), DECL_SOURCE_LINE (decl), warn),
         diagnostic_message_length_per_line);
    }
  else
    report_file_and_line (DECL_SOURCE_FILE (decl),
                          DECL_SOURCE_LINE (decl), warn);

  /* Do magic to get around lack of varargs support for insertion
     of arguments into existing list.  We know that the decl is first;
     we ass_u_me that it will be printed with "%s".  */

  for (p = _(msgid); *p; ++p)
    {
      if (*p == '%')
	{
	  if (*(p + 1) == '%')
	    ++p;
	  else if (*(p + 1) != 's')
	    abort ();
	  else
	    break;
	}
    }

  if (p > _(msgid))			/* Print the left-hand substring.  */
    {
      if (doing_line_wrapping ())
        output_printf (&buffer, "%.*s", (int)(p - _(msgid)), _(msgid));
      else
        fprintf (stderr, "%.*s", (int)(p - _(msgid)), _(msgid));
    }

  if (*p == '%')		/* Print the name.  */
    {
      const char *n = (DECL_NAME (decl)
		 ? (*decl_printable_name) (decl, 2)
		 : _("((anonymous))"));
      if (doing_line_wrapping ())
        output_add_string (&buffer, n);
      else
        fputs (n, stderr);
      while (*p)
	{
	  ++p;
	  if (ISALPHA (*(p - 1) & 0xFF))
	    break;
	}
    }

  if (*p)			/* Print the rest of the message.  */
    {
      if (doing_line_wrapping ())
        {
	  va_copy (buffer.format_args, ap);
          output_do_printf (&buffer, p);
          va_copy (ap, buffer.format_args);
        }
      else
        vfprintf (stderr, p, ap);
    }

  if (doing_line_wrapping())
    {
      output_to_stream (&buffer, stderr);
      output_destroy_prefix (&buffer);
    }
  
  fputc ('\n', stderr);
}

/* Figure file and line of the given INSN.  */

static void
file_and_line_for_asm (insn, pfile, pline)
     rtx insn;
     const char **pfile;
     int *pline;
{
  rtx body = PATTERN (insn);
  rtx asmop;

  /* Find the (or one of the) ASM_OPERANDS in the insn.  */
  if (GET_CODE (body) == SET && GET_CODE (SET_SRC (body)) == ASM_OPERANDS)
    asmop = SET_SRC (body);
  else if (GET_CODE (body) == ASM_OPERANDS)
    asmop = body;
  else if (GET_CODE (body) == PARALLEL
	   && GET_CODE (XVECEXP (body, 0, 0)) == SET)
    asmop = SET_SRC (XVECEXP (body, 0, 0));
  else if (GET_CODE (body) == PARALLEL
	   && GET_CODE (XVECEXP (body, 0, 0)) == ASM_OPERANDS)
    asmop = XVECEXP (body, 0, 0);
  else
    asmop = NULL;

  if (asmop)
    {
      *pfile = ASM_OPERANDS_SOURCE_FILE (asmop);
      *pline = ASM_OPERANDS_SOURCE_LINE (asmop);
    }
  else
    {
      *pfile = input_filename;
      *pline = lineno;
    }
}

/* Report an error at line LINE of file FILE.  */

static void
v_error_with_file_and_line (file, line, msgid, ap)
     const char *file;
     int line;
     const char *msgid;
     va_list ap;
{
  count_error (0);
  report_error_function (file);
  if (doing_line_wrapping ())
    vline_wrapper_message_with_location (file, line, 0, msgid, ap);
  else
    v_message_with_file_and_line (file, line, 0, msgid, ap);
}

/* Report an error at the declaration DECL.
   MSGID is a format string which uses %s to substitute the declaration
   name; subsequent substitutions are a la printf.  */

static void
v_error_with_decl (decl, msgid, ap)
     tree decl;
     const char *msgid;
     va_list ap;
{
  count_error (0);
  report_error_function (DECL_SOURCE_FILE (decl));
  v_message_with_decl (decl, 0, msgid, ap);
}


/* Report an error at the line number of the insn INSN.
   This is used only when INSN is an `asm' with operands,
   and each ASM_OPERANDS records its own source file and line.  */

static void
v_error_for_asm (insn, msgid, ap)
     rtx insn;
     const char *msgid;
     va_list ap;
{
  const char *file;
  int line;

  count_error (0);
  file_and_line_for_asm (insn, &file, &line);
  report_error_function (file);
  v_message_with_file_and_line (file, line, 0, msgid, ap);
}


/* Report an error at the current line number.  */

void
verror (msgid, ap)
     const char *msgid;
     va_list ap;
{
  v_error_with_file_and_line (input_filename, lineno, msgid, ap);
}


/* Report a fatal error at the current line number.  Allow a front end to
   intercept the message.  */

static void (*fatal_function) PARAMS ((const char *, va_list));

static void
vfatal (msgid, ap)
     const char *msgid;
     va_list ap;
{
   if (fatal_function != 0)
     (*fatal_function) (_(msgid), ap);

  verror (msgid, ap);
  exit (FATAL_EXIT_CODE);
}

/* Report a warning at line LINE of file FILE.  */

static void
v_warning_with_file_and_line (file, line, msgid, ap)
     const char *file;
     int line;
     const char *msgid;
     va_list ap;
{
  if (count_error (1))
    {
      report_error_function (file);
      if (doing_line_wrapping ())
        vline_wrapper_message_with_location (file, line, 1, msgid, ap);
      else
        v_message_with_file_and_line (file, line, 1, msgid, ap);
    }
}


/* Report a warning at the declaration DECL.
   MSGID is a format string which uses %s to substitute the declaration
   name; subsequent substitutions are a la printf.  */

static void
v_warning_with_decl (decl, msgid, ap)
     tree decl;
     const char *msgid;
     va_list ap;
{
  if (count_error (1))
    {
      report_error_function (DECL_SOURCE_FILE (decl));
      v_message_with_decl (decl, 1, msgid, ap);
    }
}


/* Report a warning at the line number of the insn INSN.
   This is used only when INSN is an `asm' with operands,
   and each ASM_OPERANDS records its own source file and line.  */

static void
v_warning_for_asm (insn, msgid, ap)
     rtx insn;
     const char *msgid;
     va_list ap;
{
  if (count_error (1))
    {
      const char *file;
      int line;

      file_and_line_for_asm (insn, &file, &line);
      report_error_function (file);
      v_message_with_file_and_line (file, line, 1, msgid, ap);
    }
}


/* Report a warning at the current line number.  */

void
vwarning (msgid, ap)
     const char *msgid;
     va_list ap;
{
  v_warning_with_file_and_line (input_filename, lineno, msgid, ap);
}

/* These functions issue either warnings or errors depending on
   -pedantic-errors.  */

void
vpedwarn (msgid, ap)
     const char *msgid;
     va_list ap;
{
  if (flag_pedantic_errors)
    verror (msgid, ap);
  else
    vwarning (msgid, ap);
}


static void
v_pedwarn_with_decl (decl, msgid, ap)
     tree decl;
     const char *msgid;
     va_list ap;
{
  /* We don't want -pedantic-errors to cause the compilation to fail from
     "errors" in system header files.  Sometimes fixincludes can't fix what's
     broken (eg: unsigned char bitfields - fixing it may change the alignment
     which will cause programs to mysteriously fail because the C library
     or kernel uses the original layout).  There's no point in issuing a
     warning either, it's just unnecessary noise.  */

  if (! DECL_IN_SYSTEM_HEADER (decl))
    {
      if (flag_pedantic_errors)
	v_error_with_decl (decl, msgid, ap);
      else
	v_warning_with_decl (decl, msgid, ap);
    }
}


static void
v_pedwarn_with_file_and_line (file, line, msgid, ap)
     const char *file;
     int line;
     const char *msgid;
     va_list ap;
{
  if (flag_pedantic_errors)
    v_error_with_file_and_line (file, line, msgid, ap);
  else
    v_warning_with_file_and_line (file, line, msgid, ap);
}


/* Apologize for not implementing some feature.  */

static void
vsorry (msgid, ap)
     const char *msgid;
     va_list ap;
{
  sorrycount++;
  if (input_filename)
    fprintf (stderr, "%s:%d: ", input_filename, lineno);
  else
    fprintf (stderr, "%s: ", progname);
  notice ("sorry, not implemented: ");
  vnotice (stderr, msgid, ap);
  fputc ('\n', stderr);
}


/* Count an error or warning.  Return 1 if the message should be printed.  */

int
count_error (warningp)
     int warningp;
{
  if (warningp && inhibit_warnings)
    return 0;

  if (warningp && !warnings_are_errors)
    warningcount++;
  else
    {
      static int warning_message = 0;

      if (warningp && !warning_message)
	{
	  verbatim ("%s: warnings being treated as errors\n", progname);
	  warning_message = 1;
	}
      errorcount++;
    }

  return 1;
}

/* Print a diagnistic MSGID on FILE.  */
void
fnotice VPARAMS ((FILE *file, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  FILE *file;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  file = va_arg (ap, FILE *);
  msgid = va_arg (ap, const char *);
#endif

  vnotice (file, msgid, ap);
  va_end (ap);
}


/* Print a fatal error message.  NAME is the text.
   Also include a system error message based on `errno'.  */

void
pfatal_with_name (name)
  const char *name;
{
  fprintf (stderr, "%s: ", progname);
  perror (name);
  exit (FATAL_EXIT_CODE);
}

void
fatal_io_error (name)
  const char *name;
{
  notice ("%s: %s: I/O error\n", progname, name);
  exit (FATAL_EXIT_CODE);
}

/* Issue a pedantic warning MSGID.  */
void
pedwarn VPARAMS ((const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  msgid = va_arg (ap, const char *);
#endif

  vpedwarn (msgid, ap);
  va_end (ap);
}

/* Issue a pedantic waring about DECL.  */
void
pedwarn_with_decl VPARAMS ((tree decl, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  tree decl;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  decl = va_arg (ap, tree);
  msgid = va_arg (ap, const char *);
#endif

  v_pedwarn_with_decl (decl, msgid, ap);
  va_end (ap);
}

/* Same as above but within the context FILE and LINE. */
void
pedwarn_with_file_and_line VPARAMS ((const char *file, int line,
				     const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *file;
  int line;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  file = va_arg (ap, const char *);
  line = va_arg (ap, int);
  msgid = va_arg (ap, const char *);
#endif

  v_pedwarn_with_file_and_line (file, line, msgid, ap);
  va_end (ap);
}

/* Just apologize with MSGID.  */
void
sorry VPARAMS ((const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  msgid = va_arg (ap, const char *);
#endif

  vsorry (msgid, ap);
  va_end (ap);
}

/* Called when the start of a function definition is parsed,
   this function prints on stderr the name of the function.  */

void
announce_function (decl)
     tree decl;
{
  if (! quiet_flag)
    {
      if (rtl_dump_and_exit)
	fprintf (stderr, "%s ", IDENTIFIER_POINTER (DECL_NAME (decl)));
      else
        {
          if (doing_line_wrapping ())
            line_wrapper_printf
              (stderr, " %s", (*decl_printable_name) (decl, 2));
          else
            fprintf (stderr, " %s", (*decl_printable_name) (decl, 2));
        }
      fflush (stderr);
      need_error_newline = 1;
      last_error_function = current_function_decl;
    }
}

/* The default function to print out name of current function that caused
   an error.  */

void
default_print_error_function (file)
  const char *file;
{
  if (last_error_function != current_function_decl)
    {
      char *prefix = NULL;
      output_buffer buffer;
      
      if (file)
        prefix = build_message_string ("%s: ", file);

      if (doing_line_wrapping ())
        init_output_buffer
          (&buffer, prefix, diagnostic_message_length_per_line);
      else
        {
          if (file)
            fprintf (stderr, "%s: ", file);
        }
      
      if (current_function_decl == NULL)
        {
          if (doing_line_wrapping ())
            output_printf (&buffer, "At top level:\n");
          else
            notice ("At top level:\n");
        }
      else
	{
	  if (TREE_CODE (TREE_TYPE (current_function_decl)) == METHOD_TYPE)
            {
              if (doing_line_wrapping ())
                output_printf
                  (&buffer, "In method `%s':\n",
                   (*decl_printable_name) (current_function_decl, 2));
              else
                notice ("In method `%s':\n",
                        (*decl_printable_name) (current_function_decl, 2));
            }
	  else
            {
              if (doing_line_wrapping ())
                output_printf
                  (&buffer, "In function `%s':\n",
                   (*decl_printable_name) (current_function_decl, 2));
              else
                notice ("In function `%s':\n",
                        (*decl_printable_name) (current_function_decl, 2));
            }
	}

      last_error_function = current_function_decl;

      if (doing_line_wrapping ())
        output_to_stream (&buffer, stderr);
      
      free ((char*) prefix);
    }
}

/* Prints out, if necessary, the name of the current function
  that caused an error.  Called from all error and warning functions.
  We ignore the FILE parameter, as it cannot be relied upon.  */

void
report_error_function (file)
  const char *file ATTRIBUTE_UNUSED;
{
  struct file_stack *p;

  if (need_error_newline)
    {
      verbatim ("\n");
      need_error_newline = 0;
    }

  if (input_file_stack && input_file_stack->next != 0
      && input_file_stack_tick != last_error_tick)
    {
      for (p = input_file_stack->next; p; p = p->next)
	if (p == input_file_stack->next)
	  verbatim ("In file included from %s:%d", p->name, p->line);
	else
	  verbatim (",\n                 from %s:%d", p->name, p->line);
      verbatim (":\n");
      last_error_tick = input_file_stack_tick;
    }

  (*print_error_function) (input_filename);
}

void
error_with_file_and_line VPARAMS ((const char *file, int line,
				   const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *file;
  int line;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  file = va_arg (ap, const char *);
  line = va_arg (ap, int);
  msgid = va_arg (ap, const char *);
#endif

  v_error_with_file_and_line (file, line, msgid, ap);
  va_end (ap);
}

void
error_with_decl VPARAMS ((tree decl, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  tree decl;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  decl = va_arg (ap, tree);
  msgid = va_arg (ap, const char *);
#endif

  v_error_with_decl (decl, msgid, ap);
  va_end (ap);
}

void
error_for_asm VPARAMS ((rtx insn, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  rtx insn;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  insn = va_arg (ap, rtx);
  msgid = va_arg (ap, const char *);
#endif

  v_error_for_asm (insn, msgid, ap);
  va_end (ap);
}

void
error VPARAMS ((const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  msgid = va_arg (ap, const char *);
#endif

  verror (msgid, ap);
  va_end (ap);
}

/* Set the function to call when a fatal error occurs.  */

void
set_fatal_function (f)
     void (*f) PARAMS ((const char *, va_list));
{
  fatal_function = f;
}

void
fatal VPARAMS ((const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  msgid = va_arg (ap, const char *);
#endif

  vfatal (msgid, ap);
  va_end (ap);
}

void
_fatal_insn (msgid, insn, file, line, function)
     const char *msgid;
     rtx insn;
     const char *file;
     int line;
     const char *function;
{
  error ("%s", msgid);
  debug_rtx (insn);
  fancy_abort (file, line, function);
}

void
_fatal_insn_not_found (insn, file, line, function)
     rtx insn;
     const char *file;
     int line;
     const char *function;
{
  if (INSN_CODE (insn) < 0)
    _fatal_insn ("Unrecognizable insn:", insn, file, line, function);
  else
    _fatal_insn ("Insn does not satisfy its constraints:",
		insn, file, line, function);
}

void
warning_with_file_and_line VPARAMS ((const char *file, int line,
				     const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *file;
  int line;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  file = va_arg (ap, const char *);
  line = va_arg (ap, int);
  msgid = va_arg (ap, const char *);
#endif

  v_warning_with_file_and_line (file, line, msgid, ap);
  va_end (ap);
}

void
warning_with_decl VPARAMS ((tree decl, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  tree decl;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  decl = va_arg (ap, tree);
  msgid = va_arg (ap, const char *);
#endif

  v_warning_with_decl (decl, msgid, ap);
  va_end (ap);
}

void
warning_for_asm VPARAMS ((rtx insn, const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  rtx insn;
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  insn = va_arg (ap, rtx);
  msgid = va_arg (ap, const char *);
#endif

  v_warning_for_asm (insn, msgid, ap);
  va_end (ap);
}

void
warning VPARAMS ((const char *msgid, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *msgid;
#endif
  va_list ap;

  VA_START (ap, msgid);

#ifndef ANSI_PROTOTYPES
  msgid = va_arg (ap, const char *);
#endif

  vwarning (msgid, ap);
  va_end (ap);
}

/* Save BUFFER's STATE.  */
static void
save_output_state (buffer, state)
     const output_buffer *buffer;
     output_state *state;
{
  state->prefix = buffer->prefix;
  state->maximum_length = buffer->maximum_length;
  state->ideal_maximum_length = buffer->ideal_maximum_length;
  state->emitted_prefix_p = buffer->emitted_prefix_p;
  state->prefixing_rule = buffer->prefixing_rule;
  state->cursor = buffer->cursor;
  va_copy (state->format_args, buffer->format_args);
}

/* Restore BUFFER's previously saved STATE.  */
static void
restore_output_state (state, buffer)
     const output_state *state;
     output_buffer *buffer;
{
  buffer->prefix = state->prefix;
  buffer->maximum_length = state->maximum_length;
  buffer->ideal_maximum_length = state->ideal_maximum_length;
  buffer->emitted_prefix_p = state->emitted_prefix_p;
  buffer->prefixing_rule = state->prefixing_rule;
  buffer->cursor = state->cursor;
  va_copy (buffer->format_args, state->format_args);
}

/* Flush diagnostic_buffer content on stderr.  */
static void
finish_diagnostic ()
{
  output_to_stream (diagnostic_buffer, stderr);
  fputc ('\n', stderr);
  fflush (stderr);
}

/* Helper subroutine of output_verbatim and verbatim. Do the approriate
   settings needed by BUFFER for a verbatim formatting.  */
static void
output_do_verbatim (buffer, msg, args)
     output_buffer *buffer;
     const char *msg;
     va_list args;
{
  output_state os;

  save_output_state (buffer, &os);
  buffer->prefix = NULL;
  buffer->prefixing_rule = DIAGNOSTICS_SHOW_PREFIX_NEVER;
  buffer->cursor = msg;
  va_copy (buffer->format_args, args);
  output_set_maximum_length (buffer, 0);
  output_format (buffer);
  va_end (buffer->format_args);
  restore_output_state (&os, buffer);
}

/* Output MESSAGE verbatim into BUFFER.  */
void
output_verbatim VPARAMS ((output_buffer *buffer, const char *msg, ...))
{
#ifndef ANSI_PROTOTYPES
  output_buffer *buffer;
  const char *msg;
#endif
  va_list ap;

  VA_START (ap, msg);
#ifndef ANSI_PROTOTYPES
  buffer = va_arg (ap, output_buffer *);
  msg = va_arg (ap, const char *);
#endif
  output_do_verbatim (buffer, msg, ap);
}

/* Same as above but use diagnostic_buffer.  */
void
verbatim VPARAMS ((const char *msg, ...))
{
#ifndef ANSI_PROTOTYPES
  const char *msg;
#endif
  va_list ap;

  VA_START (ap, msg);
#ifndef ANSI_PROTOTYPES
  msg = va_arg (ap, const char *);
#endif
  output_do_verbatim (diagnostic_buffer, msg, ap);
  output_to_stream (diagnostic_buffer, stderr);
}

/* Report a diagnostic MESSAGE (an error or a WARNING) involving
   entities in ARGUMENTS.  FILE and LINE indicate where the diagnostic
   occurs.  This function is *the* subroutine in terms of which front-ends
   should implement their specific diagnostic handling modules.  */
void
report_diagnostic (msg, args, file, line, warn)
     const char *msg;
     va_list args;
     const char *file;
     int line;
     int warn;
{
  output_state os;

  save_output_state (diagnostic_buffer, &os);
  diagnostic_msg = msg;
  va_copy (diagnostic_args, args);
  if (count_error (warn))
    {
      report_error_function (file);
      output_set_prefix
        (diagnostic_buffer, context_as_prefix (file, line, warn));
      output_format (diagnostic_buffer);
      finish_diagnostic();
      output_destroy_prefix (diagnostic_buffer);
    }
  va_end (diagnostic_args);
  restore_output_state (&os, diagnostic_buffer);
}
