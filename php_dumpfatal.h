#ifndef PHP_DUMPFATAL_H
#define PHP_DUMPFATAL_H

extern zend_module_entry dumpfatal_module_entry;
#define phpext_dumpfatal_ptr &dumpfatal_module_entry

#ifdef ZTS
#include "TSRM.h"
#endif

#define PHP_DUMPFATAL_VERSION "0.0.1"

#define DUMPFATAL_INTERNAL      1
#define DUMPFATAL_EXTERNAL      2

#define DUMPFATAL_MAX_LINE 260
#define DUMPFATAL_MAX_TYPE_LINE 10
#define DUMPFATAL_MAX_LINES 50
#define DUMPFATAL_MAX_STR 29

typedef struct _dumpfatal_traceline_t {
  char file[ DUMPFATAL_MAX_LINE + 1 ];
  zend_ushort line;
  char function[ DUMPFATAL_MAX_LINE + 1 ];
  char class_name[ DUMPFATAL_MAX_LINE + 1 ];
  char object_name[ DUMPFATAL_MAX_LINE + 1 ];
  char call_type[ DUMPFATAL_MAX_TYPE_LINE + 1 ];
  char args[ DUMPFATAL_MAX_LINE + 1 ];
} dumpfatal_traceline_t;

typedef struct _dumpfatal_trace_t {
  dumpfatal_traceline_t lines[ DUMPFATAL_MAX_LINES ];
  zend_ushort lines_used;
  zend_ushort line_current;
} dumpfatal_trace_t;

ZEND_BEGIN_MODULE_GLOBALS(dumpfatal)
  dumpfatal_trace_t trace;
  char *info;
  int info_len;
  zend_bool enabled;
  char *dateformat;
ZEND_END_MODULE_GLOBALS(dumpfatal)

#ifdef ZTS
#define DFT_G(v) TSRMG(dumpfatal_globals_id, zend_dumpfatal_globals *, v)
#else
#define DFT_G(v) (dumpfatal_globals.v)
#endif

#endif	/* PHP_DUMPFATAL_H */
