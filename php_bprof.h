#ifndef PHP_BPROF_H
#define PHP_BPROF_H

extern zend_module_entry bprof_module_entry;
#define phpext_bprof_ptr &bprof_module_entry

#ifdef PHP_WIN32
#define PHP_BPROF_API __declspec(dllexport)
#else
#define PHP_BPROF_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#include "zend_observer.h"

#define BPROF_VERSION "1.4"
#define BPROF_FUNC_HASH_COUNTERS_SIZE 1024
#define ROOT_SYMBOL "main()"
#define SCRATCH_BUF_LEN 512

#define BPROF_FLAGS_NO_BUILTINS 0x0001
#define BPROF_FLAGS_MEMORY 0x0004

#if !defined(uint32)
typedef unsigned int uint32;
#endif

#if !defined(uint8)
typedef unsigned char uint8;
#endif

#define register_trace_callback(function_name, cb) zend_hash_str_update_mem(BPROF_G(trace_callbacks), function_name, sizeof(function_name) - 1, &cb, sizeof(bp_trace_callback));

/* bprof maintains a stack of entries being profiled. The memory for the entry
 * is passed by the layer that invokes BEGIN_PROFILING(), e.g. the bp_execute()
 * function. Often, this is just C-stack memory.
 *
 * This structure is a convenient place to track start time of a particular
 * profile operation, recursion depth, and the name of the function being
 * profiled. */
typedef struct bp_entry_t {
    struct bp_entry_t *prev_bprof; // ptr to prev entry being profiled
    zend_string *name_bprof; // function name
    int rlvl_bprof; // recursion level for function
    long int mu_start_bprof; // memory usage
    long int pmu_start_bprof; // peak memory usage
    zend_ulong tsc_start; // start value for TSC counter
    zend_ulong hash_code; // hash_code for the function name
    int is_trace;
} bp_entry_t;

typedef zend_string *(*bp_trace_callback)(zend_string *symbol, zend_execute_data *data);

/* Various types for BPROF callbacks       */
typedef void (*bp_init_cb)();

typedef void (*bp_exit_cb)();

typedef void (*bp_begin_function_cb)(bp_entry_t **entries, bp_entry_t *current);

typedef void (*bp_end_function_cb)(bp_entry_t **entries);

/**
 * ***********************
 * GLOBAL STATIC VARIABLES
 * ***********************
 */

/* Pointer to the origianl execute_internal function */
static void (*_zend_execute_internal)(zend_execute_data *data, zval *return_value);

ZEND_DLEXPORT void bp_execute_internal(zend_execute_data *execute_data, zval *return_value);

static zend_op_array * (*_zend_compile_file) (zend_file_handle *file_handle, int type);
ZEND_DLEXPORT zend_op_array* bp_compile_file(zend_file_handle *file_handle, int type);

#if PHP_VERSION_ID >= 80200

/* Pointer to the original compile string function (used by eval) */
static zend_op_array * (*_zend_compile_string) (zend_string *source_string, const char *filename, zend_compile_position position);
ZEND_DLEXPORT zend_op_array* bp_compile_string(zend_string *source_string, const char *filename, zend_compile_position position);

static zend_observer_fcall_handlers tracer_observer(zend_execute_data *execute_data);
static void tracer_observer_begin(zend_execute_data *ex);
static void tracer_observer_end(zend_execute_data *ex, zval *return_value);
#else
/* Pointer to the original compile string function (used by eval) */
static zend_op_array * (*_zend_compile_string) (zend_string *source_string, const char *filename);
ZEND_DLEXPORT zend_op_array* bp_compile_string(zend_string *source_string, const char *filename);

static zend_observer_fcall_handlers tracer_observer(zend_execute_data *execute_data);
static void tracer_observer_begin(zend_execute_data *ex);
static void tracer_observer_end(zend_execute_data *ex, zval *return_value);
#endif

/**
 * ****************************
 * STATIC FUNCTION DECLARATIONS
 * ****************************
 */
static void bp_register_constants(INIT_FUNC_ARGS);

static void bp_begin(zend_long bprof_flags);

static void bp_stop();

static void bp_end();

static inline zend_ulong cycle_timer();

static void bp_free_the_free_list();

static void bp_fast_free_bprof_entry(bp_entry_t *p);

void bp_init_trace_callbacks();

double get_timebase_conversion();

/* Struct to hold the various callbacks for a single bprof mode */
typedef struct bp_mode_cb {
    bp_init_cb init_cb;
    bp_exit_cb exit_cb;
    bp_begin_function_cb begin_fn_cb;
    bp_end_function_cb end_fn_cb;
} bp_mode_cb;

/* Bprof's global state.
 *
 * This structure is instantiated once.  Initialize defaults for attributes in
 * bp_init_profiler_state() Cleanup/free attributes in
 * bp_clean_profiler_state() */
ZEND_BEGIN_MODULE_GLOBALS(bprof)

/*       ----------   Global attributes:  -----------       */

/* Indicates if bprof is currently enabled */
int enabled;

/* Indicates if bprof was ever enabled during this request */
int ever_enabled;

/* Holds all the bprof statistics */
zval stats_count;

/* Indicates the current bprof mode or level */
int profiler_level;

/* Top of the profile stack */
bp_entry_t *entries;

/* freelist of bp_entry_t chunks for reuse... */
bp_entry_t *entry_free_list;

/* Callbacks for various bprof modes */
bp_mode_cb mode_cb;

/*       ----------   Mode specific attributes:  -----------       */

/* Global to track the time of the last sample in time and ticks */
struct timeval last_sample_time;
zend_ulong last_sample_tsc;
/* BPROF_SAMPLING_INTERVAL in ticks */
zend_long sampling_interval;
zend_ulong sampling_interval_tsc;
zend_long sampling_depth;
/* bprof flags */
uint32 bprof_flags;

zend_string *root;

/* counter table indexed by hash value of function names. */
zend_ulong func_hash_counters[BPROF_FUNC_HASH_COUNTERS_SIZE];

HashTable *trace_callbacks;

double timebase_conversion;

zend_bool collect_additional_info;

ZEND_END_MODULE_GLOBALS(bprof)

PHP_MINIT_FUNCTION(bprof);
PHP_MSHUTDOWN_FUNCTION(bprof);
PHP_RINIT_FUNCTION(bprof);
PHP_RSHUTDOWN_FUNCTION(bprof);
PHP_MINFO_FUNCTION(bprof);

PHP_FUNCTION(bprof_enable);
PHP_FUNCTION(bprof_disable);

#ifdef ZTS
#define BPROF_G(v) TSRMG(bprof_globals_id, zend_bprof_globals *, v)
#else
#define BPROF_G(v) (bprof_globals.v)
#endif

extern ZEND_DECLARE_MODULE_GLOBALS(bprof);
#endif /* PHP_BPROF_H */
