#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/date/php_date.h"
#include "php_dumpfatal.h"
#include "Zend/zend_extensions.h"
#include "SAPI.h"

#include <stdio.h>
#include <fcntl.h>
#include "TSRM/tsrm_virtual_cwd.h"

ZEND_DECLARE_MODULE_GLOBALS(dumpfatal)

#ifdef COMPILE_DL_DUMPFATAL
ZEND_GET_MODULE(dumpfatal)
#endif

#ifndef Z_OBJ_P
#define Z_OBJ_P(zval_p) \
        ((zend_object*)(EG(objects_store).object_buckets[Z_OBJ_HANDLE_P(zval_p)].bucket.obj.object))
#endif

/* Static global variables */
#if PHP_VERSION_ID < 50500
void (*dumpfatal_old_execute)(zend_op_array *op_array TSRMLS_DC);
void (*dumpfatal_old_execute_internal)(zend_execute_data *current_execute_data, int return_value_used TSRMLS_DC);
#else
void (*dumpfatal_old_execute_ex)(zend_execute_data *execute_data TSRMLS_DC);
void (*dumpfatal_old_execute_internal)(zend_execute_data *current_execute_data, struct _zend_fcall_info *fci, int return_value_used TSRMLS_DC);
#endif

static void (*dumpfatal_old_error_handler)(int, const char *, const uint, const char *, va_list);

//----------------------------Common functions----------------------------------

static zend_uint dumpfatal_var_export(zval **struc, char *buf, zend_uint max_len TSRMLS_DC)
{
  HashTable *myht;
  zend_uint retval;
  zend_class_entry *ce;

  switch (Z_TYPE_PP(struc)) {
  case IS_BOOL:
    if (Z_LVAL_PP(struc)) {
      strncpy(buf, "true", max_len > 4 ? 4 : max_len );
      return 4;
    } else {
      strncpy(buf, "false", max_len > 5 ? 5 : max_len );
      return 5;
    }
  case IS_NULL:
    strncpy(buf, "NULL", max_len > 4 ? 4 : max_len );
    return 4;
  case IS_LONG:
    snprintf(buf, max_len, "%ld", Z_LVAL_PP(struc) );
    return strlen(buf);
  case IS_DOUBLE:
    snprintf(buf, max_len, "%.*G", 1, Z_DVAL_PP(struc));
    return strlen(buf);
  case IS_STRING:
    if( strlen(Z_STRVAL_PP(struc)) > (DUMPFATAL_MAX_STR + 3) && max_len > DUMPFATAL_MAX_STR ) {
      retval = snprintf(buf, DUMPFATAL_MAX_STR + 1, "'%s", Z_STRVAL_PP(struc));
      if(retval > DUMPFATAL_MAX_STR + 1) {
        char *tmpbuf = buf + strlen(buf);
        snprintf(tmpbuf, max_len - DUMPFATAL_MAX_STR - 1, "...'");
      }
    }
    else {
      snprintf(buf, max_len, "'%s'", Z_STRVAL_PP(struc));
    }
    return strlen(buf);
  case IS_ARRAY:
    myht = Z_ARRVAL_PP(struc);
    snprintf(buf, max_len, "array[%i]", zend_hash_num_elements(myht));
    return strlen(buf);
  case IS_OBJECT:
    ce = Z_OBJCE_PP(struc);
    if( ce ) {
      int len = strlen(ce->name);
      strncpy(buf, ce->name, max_len > len ? len : max_len);
      return len;
    }
    else {
      strncpy(buf, "CERR", max_len > 4 ? 4 : max_len);
      return 4;
    }
  default:
    strncpy(buf, "NULL",  max_len > 4 ? 4 : max_len);
    return 4;
  }

  return 0;
}

static void dumpfatal_get_args(char *ptr, void **curpos TSRMLS_DC)
{
  zend_ushort remains = DUMPFATAL_MAX_LINE;
  zend_uint printed;

  void **p = curpos;
  zval **arg;
  int arg_count = (int)(zend_uintptr_t) *p;
  p -= arg_count;

  while (--arg_count >= 0 && remains) {
    arg = (zval **) p++;
    if (*arg) {
      printed = dumpfatal_var_export(arg, ptr, remains TSRMLS_CC );
      if( printed < remains ) {
        remains -= printed;
        ptr += printed;
      }
      else {
        remains = 0;
      }

      if(arg_count > 0 && remains > 0) {
        strncpy(ptr, ",", 1);
        remains--;
        ptr++;
      }
    }
  }
}

static void dumpfatal_add_stack_frame(zend_execute_data *zdata, zend_op_array *op_array, int type TSRMLS_DC)
{
  if( !DFT_G(enabled) ) {
    return;
  }

  DFT_G(trace).line_current++;
  if( DFT_G(trace).lines_used >= DUMPFATAL_MAX_LINES ) {
    return;
  }

  zend_execute_data *edata, *skip;
  zend_op **opline_ptr = NULL;
  zend_op *cur_opcode;

  const char *function_name = 0;
  const char *filename = 0;
  const char *class_name = 0;
  const char *object_name = 0;
  const char *call_type = 0;
  char args[ DUMPFATAL_MAX_LINE + 1 ];
  int lineno = 0;

  memset(args, 0, sizeof(args));

#if PHP_VERSION_ID < 50500
  edata = EG(current_execute_data);
  opline_ptr = EG(opline_ptr);
#else
  if (type == DUMPFATAL_EXTERNAL) {
    edata = EG(current_execute_data)->prev_execute_data;
    if (edata) {
      opline_ptr = &edata->opline;
    }
  } else {
    edata = EG(current_execute_data);
    opline_ptr = EG(opline_ptr);
  }
#endif

  //------------------------------FILENAME + LINE-------------------------------------------
  skip = edata;
  // skip internal handler
  if ( skip &&
    !skip->op_array &&
    skip->prev_execute_data &&
    skip->prev_execute_data->opline &&
    skip->prev_execute_data->opline->opcode != ZEND_DO_FCALL &&
    skip->prev_execute_data->opline->opcode != ZEND_DO_FCALL_BY_NAME &&
    skip->prev_execute_data->opline->opcode != ZEND_INCLUDE_OR_EVAL
  ) {
    skip = skip->prev_execute_data;
  }

  if ( skip && skip->op_array ) {
    filename = skip->op_array->filename;
    lineno = skip->opline->lineno;

    // try to fetch args only if an FCALL was just made - elsewise we're in the middle of a function
    // and debug_baktrace() might have been called by the error_handler. in this case we don't
    // want to pop anything of the argument-stack
  } else if( skip ) {
    zend_execute_data *prev = skip->prev_execute_data;

    while (prev) {
      if ( prev->function_state.function &&
        prev->function_state.function->common.type != ZEND_USER_FUNCTION &&
        !( prev->function_state.function->common.type == ZEND_INTERNAL_FUNCTION
          && (prev->function_state.function->common.fn_flags & ZEND_ACC_CALL_VIA_HANDLER)
        )
      ) {
        break;
      }
      if ( prev->op_array ) {
        filename = prev->op_array->filename;
        lineno = prev->opline->lineno;
        break;
      }
      prev = prev->prev_execute_data;
    }
  }

  if( !filename && op_array && op_array->filename ) {
    filename = op_array->filename;
  }

  //--------------------------FUNCTION DETAILS-----------------------------

  if( edata ) {
    zend_function_state function_state = edata->function_state;
    zend_class_entry *ce = 0;
    if( edata->object ) {
      ce = Z_OBJCE_P(edata->object);
    }

    function_name = (function_state.function->common.scope && function_state.function->common.scope->trait_aliases) ?
        zend_resolve_method_name( ce ? ce : function_state.function->common.scope, function_state.function) :
        function_state.function->common.function_name;

    if (function_name) {
      if (edata->object && Z_TYPE_P(edata->object) == IS_OBJECT) {
        if (edata->function_state.function->common.scope) {
          class_name = edata->function_state.function->common.scope->name;
        } else {
          zend_uint class_name_len;
          zend_get_object_classname(edata->object, &class_name, &class_name_len TSRMLS_CC);
        }
        object_name = ce->name;
        call_type = "->";
      } else if (edata->function_state.function->common.scope) {
        class_name = edata->function_state.function->common.scope->name;
        call_type = "::";
      }

      if (
        (! edata->opline)
        || (
          (edata->opline->opcode == ZEND_DO_FCALL_BY_NAME)
          || (edata->opline->opcode == ZEND_DO_FCALL)
        )
      ) {
        if (edata->function_state.arguments) {
          dumpfatal_get_args(&args[0], edata->function_state.arguments TSRMLS_CC);
        }
      }
    } else {
      if (!edata->opline || edata->opline->opcode != ZEND_INCLUDE_OR_EVAL) {
        function_name = "unknown";
      }
      else switch (edata->opline->extended_value) {
        case ZEND_EVAL:
          function_name = "eval";
          break;
        case ZEND_INCLUDE:
          function_name = "include";
          break;
        case ZEND_REQUIRE:
          function_name = "require";
          break;
        case ZEND_INCLUDE_ONCE:
          function_name = "include_once";
          break;
        case ZEND_REQUIRE_ONCE:
          function_name = "require_once";
          break;
        default:
          function_name = "unknown";
          break;
      }

      if (edata->function_state.arguments) {
        dumpfatal_get_args(&args[0], edata->function_state.arguments TSRMLS_CC);
      }
    }
  }
  else {
    function_name = "{main}";
  }

  //--------------------------- STORING -----------------------------------
  zend_ushort line_idx = DFT_G(trace).lines_used;
  dumpfatal_traceline_t *line_ptr = &DFT_G(trace).lines[line_idx];
  memset(line_ptr, 0, sizeof(dumpfatal_traceline_t));
  if( filename ) {
    strcpy( line_ptr->file, filename );
    line_ptr->line = lineno;
  }

  if( function_name ) strcpy( line_ptr->function, function_name );
  if( class_name ) strcpy( line_ptr->class_name, class_name );
  if( object_name ) strcpy( line_ptr->object_name, object_name );
  if( call_type ) strcpy( line_ptr->call_type, call_type );
  memcpy(line_ptr->args, args, sizeof(args));

  DFT_G(trace).lines_used++;
}

static void dumpfatal_release_stack_frame(TSRMLS_D)
{
  if( !DFT_G(enabled) ) {
    return;
  }

  if( DFT_G(trace).line_current > 0 ) {
    DFT_G(trace).line_current--;
    if( DFT_G(trace).lines_used > DFT_G(trace).line_current ) {
      DFT_G(trace).lines_used--;
    }
  }
}

static void dumpfatal_output_error(char *error TSRMLS_DC)
{
#ifdef PHP_DISPLAY_ERRORS_STDERR
  if (PG(display_errors) == PHP_DISPLAY_ERRORS_STDERR) {
    fputs(error, stderr);
    fflush(stderr);
    return;
  }
#endif
  php_printf("%s", error);
}

PHPAPI void dumpfatal_log_err(char *log_message TSRMLS_DC)
{
  int fd = -1;
  time_t error_time;

  if (PG(in_error_log)) {
    /* prevent recursive invocation */
    return;
  }
  PG(in_error_log) = 1;

  /* Try to use the specified logging location. */
  if (PG(error_log) != NULL) {
    fd = VCWD_OPEN_MODE(PG(error_log), O_CREAT | O_APPEND | O_WRONLY, 0644);
    if (fd != -1) {
      char *tmp;
      int len;
      char *error_time_str;

      time(&error_time);
      char *dateformat = DFT_G(dateformat);
#ifdef ZTS
      if (!php_during_module_startup()) {
        error_time_str = php_format_date(dateformat, strlen(dateformat), error_time, 1 TSRMLS_CC);
      } else {
        error_time_str = php_format_date(dateformat, strlen(dateformat), error_time, 0 TSRMLS_CC);
      }
#else
      error_time_str = php_format_date(dateformat, strlen(dateformat), error_time, 1 TSRMLS_CC);
#endif
      len = spprintf(&tmp, 0, "%s %s%s", error_time_str, log_message, PHP_EOL);
      php_ignore_value(write(fd, tmp, len));
      efree(tmp);
      efree(error_time_str);
      close(fd);
      PG(in_error_log) = 0;
      return;
    }
  }

  /* Otherwise fall back to the default logging location, if we have one */
  if (sapi_module.log_message) {
    sapi_module.log_message(log_message TSRMLS_CC);
  }
  PG(in_error_log) = 0;
}

static void dumpfatal_log_stack( void (*output_function)(char * TSRMLS_DC), char *header TSRMLS_DC )
{
  char line[ 10 * DUMPFATAL_MAX_LINE];
  char lines[ DUMPFATAL_MAX_LINES * 10 * DUMPFATAL_MAX_LINE];
  char *lines_ptr = (char *)lines;

  memset(lines, 0, sizeof(lines));

  strcpy(lines_ptr, header);
  lines_ptr += strlen(header);

  strcpy(lines_ptr, "Stack trace:\n");
  lines_ptr += strlen("Stack trace:\n");

  int numPadding, filePadding = 0;
  if( DFT_G(trace).lines_used <= 10 ) {
    numPadding = 1;
  }
  else if( DFT_G(trace).lines_used <= 100 ){
    numPadding = 2;
  }
  else {
    numPadding = 3;
  }

  int i;
  for(i = DFT_G(trace).lines_used - 1; i >= 0; --i) {
    dumpfatal_traceline_t *line_ptr = &DFT_G(trace).lines[i];
    int len = strlen(line_ptr->file);
    int linelen = line_ptr->line;
    while( linelen > 9) {
      len += 1 ;
      linelen = linelen / 10;
    }
    if( filePadding == 0 || len > filePadding ) {
      filePadding = len;
    }
  }

  memset(line, 0, sizeof(line));
  sprintf(line, "%*sFILE%*sLINE\n", numPadding + 2, " ", filePadding, " ");

  strcpy(lines_ptr, line);
  lines_ptr += strlen(line);

  for(i = DFT_G(trace).lines_used - 1; i >= 0; --i) {
    memset(line, 0, sizeof(line));
    dumpfatal_traceline_t *line_ptr = &DFT_G(trace).lines[i];

    int len = strlen(line_ptr->file);
    int linelen = line_ptr->line;
    while( linelen > 9) {
      len += 1 ;
      linelen = linelen / 10;
    }

    int linenum = DFT_G(trace).lines_used - 1 - i;
    if( line_ptr->object_name[0] ) {
      sprintf(line, "#%*i %s:%i%*s<%s> %s%s%s", numPadding, linenum, line_ptr->file, line_ptr->line, filePadding - len + 2, " ", line_ptr->object_name, line_ptr->class_name, line_ptr->call_type, line_ptr->function);
    }
    else {
      sprintf(line, "#%*i %s:%i%*s%s%s%s", numPadding, linenum, line_ptr->file, line_ptr->line, filePadding - len + 2, " ", line_ptr->class_name, line_ptr->call_type, line_ptr->function);
    }

    strcpy(lines_ptr, line);
    lines_ptr += strlen(line);

    memset(line, 0, sizeof(line));
    if( line_ptr->args[0] ) {
      sprintf(line, "(%s)\n", line_ptr->args);
    }
    else {
      sprintf(line, "\n", line_ptr->args);
    }

    strcpy(lines_ptr, line);
    lines_ptr += strlen(line);
  }

  strncpy(lines_ptr, DFT_G(info), DFT_G(info_len));
  lines_ptr += DFT_G(info_len);

  strcpy(lines_ptr, "\n");
  lines_ptr += 1;
  output_function(lines TSRMLS_CC);
}

ZEND_DLEXPORT void dumpfatal_error_handler(int type, const char *error_filename, const uint error_lineno, const char *format, va_list args)
{
  //будем обрабатывать только фаталы
  // ошибки при компиляции и при запуске обрабатывать не будем
  // они не происходят в продакшн
  switch ( type ) {
    case E_ERROR:
      /* fatal errors are real errors and cannot be made exceptions */
      break;
    default:
      dumpfatal_old_error_handler( type, error_filename, error_lineno, format, args );
      return;
  }

  //не будем перехватывать исключения. у них уже есть трейс
  if( memcmp(format, "Uncaught", sizeof("Uncaught") - 1) == 0 ) {
    dumpfatal_old_error_handler( type, error_filename, error_lineno, format, args );
    return;
  }

  TSRMLS_FETCH();
  if( !DFT_G(enabled)) {
    dumpfatal_old_error_handler( type, error_filename, error_lineno, format, args );
    return;
  }

  char buffer[ 10 * DUMPFATAL_MAX_LINE ];
  memset(buffer, 0, sizeof(buffer));
  vsnprintf(buffer, sizeof(buffer) - 1, format, args);

  char header_line[ 10 * DUMPFATAL_MAX_LINE ];
  memset(header_line, 0, sizeof(header_line));
  snprintf( header_line, sizeof(header_line) - 1, "Fatal error: %s in %s on line %d\n", buffer, error_filename, error_lineno);

  if (EG(error_reporting) & type) {
    /* Log to logger */
    if (PG(log_errors)) {
      dumpfatal_log_stack( dumpfatal_log_err, header_line TSRMLS_CC);
    }
  }

  /* Display errors */
  if (PG(display_errors) && !PG(during_request_startup)) {
    dumpfatal_log_stack( dumpfatal_output_error, header_line TSRMLS_CC );
  }

  /* Bail out if we can't recover */
  switch (type) {
    case E_CORE_ERROR:
      if (!php_get_module_initialized()) {
        /* bad error in module startup - no way we can live with this */
        exit(-2);
      }
      break;
    case E_ERROR:
    case E_RECOVERABLE_ERROR:
    case E_PARSE:
    case E_COMPILE_ERROR:
    case E_USER_ERROR:
      EG(exit_status) = 255;
      if (php_get_module_initialized()) {
        if ( !PG(display_errors)
          && !SG(headers_sent)
          && SG(sapi_headers).http_response_code == 200
        ) {
          sapi_header_line ctr = {0};
          ctr.line = "HTTP/1.0 500 Internal Server Error";
          ctr.line_len = sizeof("HTTP/1.0 500 Internal Server Error") - 1;
          sapi_header_op(SAPI_HEADER_REPLACE, &ctr TSRMLS_CC);
        }
        /* the parser would return 1 (failure), we can bail out nicely */
        if (type != E_PARSE) {
          zend_objects_store_mark_destructed(&EG(objects_store) TSRMLS_CC);
          zend_bailout();
          return;
        }
      }
      break;
  }
}

#if PHP_VERSION_ID < 50500
void dumpfatal_execute(zend_op_array *op_array TSRMLS_DC)
{
  zend_execute_data *edata = EG(current_execute_data);
#else
void dumpfatal_execute_ex(zend_execute_data *execute_data TSRMLS_DC)
{
  zend_op_array *op_array = execute_data->op_array;
  zend_execute_data *edata = execute_data->prev_execute_data;
#endif

  /* if we're in a ZEND_EXT_STMT, we ignore this function call as it's likely
     that it's just being called to check for breakpoints with conditions */
  if (edata && edata->opline && edata->opline->opcode == ZEND_EXT_STMT) {
#if PHP_VERSION_ID < 50500
    dumpfatal_old_execute(op_array TSRMLS_CC);
#else
    dumpfatal_old_execute_ex(execute_data TSRMLS_CC);
#endif
    return;
  }

  dumpfatal_add_stack_frame(edata, op_array, DUMPFATAL_EXTERNAL TSRMLS_CC);

#if PHP_VERSION_ID < 50500
  dumpfatal_old_execute(op_array TSRMLS_CC);
#else
  dumpfatal_old_execute_ex(execute_data TSRMLS_CC);
#endif

  dumpfatal_release_stack_frame(TSRMLS_C);
}

#if PHP_VERSION_ID < 50500
void dumpfatal_execute_internal(zend_execute_data *current_execute_data, int return_value_used TSRMLS_DC)
#else
void dumpfatal_execute_internal(zend_execute_data *current_execute_data, struct _zend_fcall_info *fci, int return_value_used TSRMLS_DC)
#endif
{
  zend_execute_data *edata = EG(current_execute_data);
  dumpfatal_add_stack_frame(edata, edata->op_array, DUMPFATAL_INTERNAL TSRMLS_CC);

#if PHP_VERSION_ID < 50500
  if (dumpfatal_old_execute_internal) {
    dumpfatal_old_execute_internal(current_execute_data, return_value_used TSRMLS_CC);
  } else {
    execute_internal(current_execute_data, return_value_used TSRMLS_CC);
  }
#else
  if (dumpfatal_old_execute_internal) {
    dumpfatal_old_execute_internal(current_execute_data, fci, return_value_used TSRMLS_CC);
  } else {
    execute_internal(current_execute_data, fci, return_value_used TSRMLS_CC);
  }
#endif

  dumpfatal_release_stack_frame(TSRMLS_C);
}

static void dumpfatal_build_stack( TSRMLS_D ) {
  zend_execute_data *ptr, *skip;
  int lineno;
  const char *function_name;
  const char *filename;
  const char *class_name;
  const char *object_name;
  const char *type;
  char args[ DUMPFATAL_MAX_LINE + 1 ];
  const char *include_filename = 0;
  zval *stack_frame;

  zend_uint traceline_idx = 0;

  ptr = EG(current_execute_data);

  while (ptr) {

    lineno = 0;
    function_name = 0;
    filename = 0;
    class_name = 0;
    object_name = 0;
    type = 0;
    memset(args, 0, sizeof(args));

    skip = ptr;
    /* skip internal handler */
    if ( !skip->op_array &&
      skip->prev_execute_data &&
      skip->prev_execute_data->opline &&
      skip->prev_execute_data->opline->opcode != ZEND_DO_FCALL &&
      skip->prev_execute_data->opline->opcode != ZEND_DO_FCALL_BY_NAME &&
      skip->prev_execute_data->opline->opcode != ZEND_INCLUDE_OR_EVAL
    ) {
      skip = skip->prev_execute_data;
    }

    if ( skip->op_array ) {
      filename = skip->op_array->filename;
      lineno = skip->opline->lineno;

      /* try to fetch args only if an FCALL was just made - elsewise we're in the middle of a function
       * and debug_baktrace() might have been called by the error_handler. in this case we don't
       * want to pop anything of the argument-stack */
    } else {
      zend_execute_data *prev = skip->prev_execute_data;

      while (prev) {
        if ( prev->function_state.function &&
          prev->function_state.function->common.type != ZEND_USER_FUNCTION &&
          !( prev->function_state.function->common.type == ZEND_INTERNAL_FUNCTION
            && (prev->function_state.function->common.fn_flags & ZEND_ACC_CALL_VIA_HANDLER)
          )
        ) {
          break;
        }
        if ( prev->op_array ) {
          filename = prev->op_array->filename;
          lineno = prev->opline->lineno;
          break;
        }
        prev = prev->prev_execute_data;
      }
    }

    zend_function_state function_state = ptr->function_state;
    function_name = (function_state.function->common.scope && function_state.function->common.scope->trait_aliases) ?
        zend_resolve_method_name( ptr->object ? Z_OBJCE_P(ptr->object) : function_state.function->common.scope, function_state.function) :
        function_state.function->common.function_name;

    if (function_name) {
      if (ptr->object && Z_TYPE_P(ptr->object) == IS_OBJECT) {
        if (ptr->function_state.function->common.scope) {
          class_name = ptr->function_state.function->common.scope->name;
        } else {
          zend_uint class_name_len;
          zend_get_object_classname(ptr->object, &class_name, &class_name_len TSRMLS_CC);
        }

        zend_object *zobj = Z_OBJ_P(ptr->object);
        zend_uint object_name_len = zobj->ce->name_length;
        object_name = zobj->ce->name;
        type = "->";
      } else if (ptr->function_state.function->common.scope) {
        class_name = ptr->function_state.function->common.scope->name;
        type = "::";
      }

      if (
        (! ptr->opline)
        || (
          (ptr->opline->opcode == ZEND_DO_FCALL_BY_NAME)
          || (ptr->opline->opcode == ZEND_DO_FCALL)
        )
      ) {
        if (ptr->function_state.arguments) {
          dumpfatal_get_args(&args[0], ptr->function_state.arguments TSRMLS_CC);
        }
      }
    } else {
      zend_bool build_filename_arg = 1;

      if (!ptr->opline || ptr->opline->opcode != ZEND_INCLUDE_OR_EVAL) {
        function_name = "unknown";
        build_filename_arg = 0;
      }
      else switch (ptr->opline->extended_value) {
        case ZEND_EVAL:
          function_name = "eval";
          build_filename_arg = 0;
          break;
        case ZEND_INCLUDE:
          function_name = "include";
          break;
        case ZEND_REQUIRE:
          function_name = "require";
          break;
        case ZEND_INCLUDE_ONCE:
          function_name = "include_once";
          break;
        case ZEND_REQUIRE_ONCE:
          function_name = "require_once";
          break;
        default:
          function_name = "unknown";
          build_filename_arg = 0;
          break;
      }

      strncpy( &args[0], include_filename, DUMPFATAL_MAX_LINE );
    }

    if( traceline_idx < DUMPFATAL_MAX_LINES ) {
      dumpfatal_traceline_t *line_ptr = &DFT_G(trace).lines[traceline_idx];
      memset(line_ptr, 0, sizeof(dumpfatal_traceline_t));

      if( filename ) {
        strncpy( line_ptr->file, filename, DUMPFATAL_MAX_LINE );
        line_ptr->line = lineno;
      }

      if( function_name ) strncpy( line_ptr->function, function_name, DUMPFATAL_MAX_LINE );
      if( class_name ) strncpy( line_ptr->class_name, class_name, DUMPFATAL_MAX_LINE );
      if( object_name ) strncpy( line_ptr->object_name, object_name, DUMPFATAL_MAX_LINE );
      if( type ) strncpy( line_ptr->call_type, type, DUMPFATAL_MAX_TYPE_LINE );
      if( args ) strncpy( line_ptr->args, args, DUMPFATAL_MAX_LINE );

      traceline_idx++;
      DFT_G(trace).lines_used++;
    }

    DFT_G(trace).line_current++;

    include_filename = filename;
    ptr = skip->prev_execute_data;
  }

  //перевернуть нужно
  int i, j;
  dumpfatal_traceline_t temp;
  for(i = 0, j = traceline_idx - 1; i < traceline_idx/2; ++i, --j) {
    temp = DFT_G(trace).lines[i];
    DFT_G(trace).lines[i] = DFT_G(trace).lines[j];
    DFT_G(trace).lines[j] = temp;
  }
}

//----------------------------Extension functions------------------------------------

//proto bool dumpfatal_set_aditional_info(string $info)
static PHP_FUNCTION(dumpfatal_set_aditional_info)
{
  char *info;
  int info_len = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &info, &info_len) != SUCCESS) {
    RETURN_FALSE;
  }

  if( DFT_G(info_len) ) {
    efree(DFT_G(info));
    DFT_G(info_len) = 0;
    DFT_G(info) = 0;
  }

  DFT_G(info) = (char *)emalloc(info_len + 1);
  strncpy(DFT_G(info), info, info_len);
  DFT_G(info)[info_len] = '\0';
  DFT_G(info_len) = info_len;

  RETURN_TRUE;
}

//proto array dumpfatal_gettrace()
static PHP_FUNCTION(dumpfatal_gettrace) {
  array_init(return_value);

  if( DFT_G(info_len) ) {
    add_assoc_stringl(return_value, "additional_info", DFT_G(info), DFT_G(info_len), 1);
  }


  int i;
  dumpfatal_traceline_t *line_ptr;
  zval *traceline;

  for( i = 0; i < DFT_G(trace).lines_used; ++i ) {
    line_ptr = &DFT_G(trace).lines[i];

    ALLOC_INIT_ZVAL(traceline);
    array_init(traceline);

    if( line_ptr->file[0] ) {
      add_assoc_string(traceline, "file", line_ptr->file, 1);
      add_assoc_long(traceline, "line", line_ptr->line);
    }

    if( line_ptr->function[0] ) add_assoc_string(traceline, "function", line_ptr->function, 1);
    if( line_ptr->class_name[0] ) add_assoc_string(traceline, "class", line_ptr->class_name, 1);
    if( line_ptr->object_name[0] ) add_assoc_string(traceline, "object", line_ptr->object_name, 1);
    if( line_ptr->call_type[0] ) add_assoc_string(traceline, "type", line_ptr->call_type, 1);
    if( line_ptr->args[0] ) add_assoc_string(traceline, "args", line_ptr->args, 1);

    add_next_index_zval(return_value, traceline);
  }
}

//--------------------------------PHP API-------------------------------------------

const zend_function_entry dumpfatal_functions[] = {
	PHP_FE(dumpfatal_set_aditional_info,	NULL)
	PHP_FE(dumpfatal_gettrace,  NULL)
	PHP_FE_END
};

static PHP_INI_MH(OnIniUpdate)
{
  if( strncmp( "dumpfatal.enabled", entry->name, entry->name_length ) == 0 ) {
    zend_bool new_enabled = atoi(new_value);
    if( DFT_G(enabled) && !new_enabled ) {
      DFT_G(trace).lines_used = 0;
      DFT_G(trace).line_current = 0;
    }
    else if ( !DFT_G(enabled) && new_enabled ) {
      dumpfatal_build_stack( TSRMLS_C );
    }
    DFT_G(enabled) = new_enabled;
  }

  return SUCCESS;
}

PHP_INI_BEGIN()
    PHP_INI_ENTRY("dumpfatal.enabled", "0", PHP_INI_ALL, OnIniUpdate)
    STD_PHP_INI_ENTRY("dumpfatal.dateformat", "[Y-m-d H:i:s]", PHP_INI_ALL, OnUpdateString, dateformat,  zend_dumpfatal_globals, dumpfatal_globals)
PHP_INI_END()

static void php_dumpfatal_init_globals(zend_dumpfatal_globals *globals)
{
  memset(globals, 0, sizeof(*globals));
}

PHP_MINIT_FUNCTION(dumpfatal)
{    
  ZEND_INIT_MODULE_GLOBALS(dumpfatal, php_dumpfatal_init_globals, NULL);
  REGISTER_INI_ENTRIES();

  dumpfatal_old_error_handler = zend_error_cb;
	zend_error_cb = dumpfatal_error_handler;
        
  dumpfatal_old_execute_internal = zend_execute_internal;
  zend_execute_internal = dumpfatal_execute_internal;

#if PHP_VERSION_ID < 50500
  dumpfatal_old_execute = zend_execute;
  zend_execute = dumpfatal_execute;
#else
  dumpfatal_old_execute_ex = zend_execute_ex;
  zend_execute_ex = dumpfatal_execute_ex;
#endif

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(dumpfatal)
{
  UNREGISTER_INI_ENTRIES();

#if PHP_VERSION_ID < 50500
  zend_execute = dumpfatal_old_execute;
#else
  zend_execute_ex = dumpfatal_old_execute_ex;
#endif

	zend_error_cb = dumpfatal_old_error_handler;
  zend_execute_internal = dumpfatal_old_execute_internal;

	return SUCCESS;
}

PHP_RINIT_FUNCTION(dumpfatal)
{
  DFT_G(trace).lines_used = 0;
  DFT_G(trace).line_current = 0;

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(dumpfatal)
{
  if( DFT_G(info_len) ) {
    efree(DFT_G(info));
    DFT_G(info_len) = 0;
    DFT_G(info) = 0;
  }

	return SUCCESS;
}

PHP_MINFO_FUNCTION(dumpfatal)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "Dumpfatal support", "enabled");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

zend_module_entry dumpfatal_module_entry = {
  STANDARD_MODULE_HEADER,
  "dumpfatal",
  dumpfatal_functions,
  PHP_MINIT(dumpfatal),
  PHP_MSHUTDOWN(dumpfatal),
  PHP_RINIT(dumpfatal),
  PHP_RSHUTDOWN(dumpfatal),
  PHP_MINFO(dumpfatal),
  PHP_DUMPFATAL_VERSION,
  STANDARD_MODULE_PROPERTIES
};
