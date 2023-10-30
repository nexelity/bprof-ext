#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "Zend/zend_smart_str.h"
#include "ext/standard/info.h"
#include "php_bprof.h"
#include "zend_extensions.h"
#include "trace.h"

#ifndef ZEND_WIN32
# include <sys/time.h>
# include <sys/resource.h>
# include <unistd.h>
#else
# include "win32/time.h"
# include "win32/getrusage.h"
# include "win32/unistd.h"
#endif
#include <stdlib.h>

#if HAVE_PCRE
#include "ext/pcre/php_pcre.h"
#endif

#if __APPLE__
#include <mach/mach_init.h>
#include <mach/mach_time.h>
#endif

#ifdef ZEND_WIN32
LARGE_INTEGER performance_frequency;
#endif


# define BPROF_DEBUG false

ZEND_DECLARE_MODULE_GLOBALS(bprof)

/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_bprof_enable, 0, 0, 0)
  ZEND_ARG_INFO(0, flags)
  ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_bprof_disable, 0)
ZEND_END_ARG_INFO()

/* }}} */

/* List of functions implemented/exposed by bprof */
zend_function_entry bprof_functions[] = {
  PHP_FE(bprof_enable, arginfo_bprof_enable)
  PHP_FE(bprof_disable, arginfo_bprof_disable)
  {NULL, NULL, NULL}
};

/* Callback functions for the bprof extension */
zend_module_entry bprof_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
        STANDARD_MODULE_HEADER,
#endif
        "bprof",                        /* Name of the extension */
        bprof_functions,                /* List of functions exposed */
        PHP_MINIT(bprof),               /* Module init callback */
        PHP_MSHUTDOWN(bprof),           /* Module shutdown callback */
        PHP_RINIT(bprof),               /* Request init callback */
        PHP_RSHUTDOWN(bprof),           /* Request shutdown callback */
        PHP_MINFO(bprof),               /* Module info callback */
#if ZEND_MODULE_API_NO >= 20010901
        BPROF_VERSION,
#endif
        STANDARD_MODULE_PROPERTIES
};

/* Init module */
#ifdef COMPILE_DL_BPROF
    ZEND_GET_MODULE(bprof)
#endif

#ifdef ZTS
    ZEND_TSRMLS_CACHE_DEFINE();
#endif

/**
 * Start bprof profiling in hierarchical mode.
 *
 * @param  long $flags  flags for hierarchical mode
 * @return void
 */
PHP_FUNCTION(bprof_enable)
{
    zend_long bprof_flags = 0;              /* bprof flags */
    zval *optional_array = NULL;         /* optional array arg: for future use */

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "|lz", &bprof_flags, &optional_array) == FAILURE) {
        return;
    }

    bp_get_ignored_functions_from_arg(optional_array);

    bp_begin(bprof_flags);
}

/**
 * Stops bprof from profiling in hierarchical mode anymore and returns the
 * profile info.
 *
 * @param  void
 * @return array  hash-array of bprof's profile info
 */
PHP_FUNCTION(bprof_disable)
{
    if (BPROF_G(enabled)) {
        bp_stop();
        RETURN_ZVAL(&BPROF_G(stats_count), 1, 0);
    }
    /* else null is returned */
}

static void php_bprof_init_globals(zend_bprof_globals *bprof_globals)
{
    bprof_globals->enabled = 0;
    bprof_globals->ever_enabled = 0;
    bprof_globals->bprof_flags = 0;
    bprof_globals->entries = NULL;
    bprof_globals->root = NULL;
    bprof_globals->trace_callbacks = NULL;
    bprof_globals->ignored_functions = NULL;

    ZVAL_UNDEF(&bprof_globals->stats_count);

    /* no free bp_entry_t structures to start with */
    bprof_globals->entry_free_list = NULL;

    int i;
    for (i = 0; i < BPROF_FUNC_HASH_COUNTERS_SIZE; i++) {
        bprof_globals->func_hash_counters[i] = 0;
    }
}

/**
 * Module init callback.
 */
PHP_MINIT_FUNCTION(bprof)
{
    ZEND_INIT_MODULE_GLOBALS(bprof, php_bprof_init_globals, NULL);

    bp_register_constants(INIT_FUNC_ARGS_PASSTHRU);

    /* Replace zend_compile with our proxy */
    _zend_compile_file = zend_compile_file;
    zend_compile_file  = bp_compile_file;

    /* Replace zend_compile_string with our proxy */
    _zend_compile_string = zend_compile_string;
    zend_compile_string = bp_compile_string;

#if PHP_VERSION_ID >= 80000
    zend_observer_fcall_register(tracer_observer);
#else
    /* Replace zend_execute with our proxy */
    _zend_execute_ex = zend_execute_ex;
    zend_execute_ex  = bp_execute_ex;
#endif

    /* Replace zend_execute_internal with our proxy */
    _zend_execute_internal = zend_execute_internal;
    zend_execute_internal = bp_execute_internal;

#if defined(DEBUG)
    /* To make it random number generator repeatable to ease testing. */
    srand(0);
#endif

#ifdef ZEND_WIN32
    QueryPerformanceFrequency(&performance_frequency);
#endif
    return SUCCESS;
}

/**
 * Module shutdown callback.
 */
PHP_MSHUTDOWN_FUNCTION(bprof)
{
    /* free any remaining items in the free list */
    bp_free_the_free_list();

#if PHP_VERSION_ID < 80000
    /* Remove proxies, restore the originals */
    zend_execute_ex       = _zend_execute_ex;
#endif
    zend_execute_internal = _zend_execute_internal;
    zend_compile_file     = _zend_compile_file;
    zend_compile_string   = _zend_compile_string;

    return SUCCESS;
}

/**
 * Request init callback. Nothing to do yet!
 */
PHP_RINIT_FUNCTION(bprof)
{
#if defined(ZTS) && defined(COMPILE_DL_BPROF)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    BPROF_G(timebase_conversion) = get_timebase_conversion();

    return SUCCESS;
}

/**
 * Request shutdown callback. Stop profiling and return.
 */
PHP_RSHUTDOWN_FUNCTION(bprof)
{
    bp_end();
    return SUCCESS;
}

/**
 * Module info callback. Returns the bprof version.
 */
PHP_MINFO_FUNCTION(bprof)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "Author", "Ben Poulson <ben.poulson@nexelity.com>");
    php_info_print_table_row(2, "Version", BPROF_VERSION);
    php_info_print_table_end();
    // Display any INI entries defined for your extension
    DISPLAY_INI_ENTRIES();
}

static void bp_register_constants(INIT_FUNC_ARGS)
{
    REGISTER_LONG_CONSTANT("BPROF_FLAGS_NO_BUILTINS", BPROF_FLAGS_NO_BUILTINS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("BPROF_FLAGS_CPU", BPROF_FLAGS_CPU, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("BPROF_FLAGS_MEMORY", BPROF_FLAGS_MEMORY, CONST_CS | CONST_PERSISTENT);
}

/**
 * Parse the list of ignored functions from the zval argument.
 *
 */
void bp_get_ignored_functions_from_arg(zval *args)
{
    if (args == NULL) {
        return;
    }

    zval *pzval = zend_hash_str_find(Z_ARRVAL_P(args), "ignored_functions", sizeof("ignored_functions") - 1);
    BPROF_G(ignored_functions) = bp_ignored_functions_init(pzval);
}

void bp_ignored_functions_clear(bp_ignored_functions *functions)
{
    if (functions == NULL) {
        return;
    }

    bp_array_del(functions->names);
    functions->names = NULL;

    memset(functions->filter, 0, BPROF_MAX_IGNORED_FUNCTIONS);
    efree(functions);
}

double get_timebase_conversion()
{
#if defined(__APPLE__)
    mach_timebase_info_data_t info;
    (void) mach_timebase_info(&info);

    return info.denom * 1000. / info.numer;
#endif

    return 1.0;
}

bp_ignored_functions *bp_ignored_functions_init(zval *values)
{
    bp_ignored_functions *functions;
    zend_string **names;
    uint32_t ix = 0;
    int count;

    /* Delete the array storing ignored function names */
    bp_ignored_functions_clear(BPROF_G(ignored_functions));

    if (!values) {
        return NULL;
    }

    if (Z_TYPE_P(values) == IS_ARRAY) {
        HashTable *ht;
        zend_ulong num_key;
        zend_string *key;
        zval *val;

        ht = Z_ARRVAL_P(values);
        count = zend_hash_num_elements(ht);

        names = ecalloc(count + 1, sizeof(zend_string *));

        ZEND_HASH_FOREACH_KEY_VAL(ht, num_key, key, val) {
            if (!key) {
                if (Z_TYPE_P(val) == IS_STRING && strcmp(Z_STRVAL_P(val), ROOT_SYMBOL) != 0) {
                    /* do not ignore "main" */
                    names[ix] = zend_string_init(Z_STRVAL_P(val), Z_STRLEN_P(val), 0);
                    ix++;
                }
            }
        } ZEND_HASH_FOREACH_END();
    } else if (Z_TYPE_P(values) == IS_STRING) {
        names = ecalloc(2, sizeof(zend_string *));
        names[0] = zend_string_init(Z_STRVAL_P(values), Z_STRLEN_P(values), 0);
        ix = 1;
    } else {
        return NULL;
    }

    /* NULL terminate the array */
    names[ix] = NULL;

    functions = emalloc(sizeof(bp_ignored_functions));
    functions->names = names;

    memset(functions->filter, 0, BPROF_MAX_IGNORED_FUNCTIONS);

    uint32_t i = 0;
    for (; names[i] != NULL; i++) {
        zend_ulong hash = ZSTR_HASH(names[i]);
        int idx = hash % BPROF_MAX_IGNORED_FUNCTIONS;
        functions->filter[idx] = hash;
    }

    return functions;
}

/**
 * Initialize profiler state
 *
 */
void bp_init_profiler_state()
{
    /* Setup globals */
    if (!BPROF_G(ever_enabled)) {
        BPROF_G(ever_enabled) = 1;
        BPROF_G(entries) = NULL;
    }

    /* Init stats_count */
    if (Z_TYPE(BPROF_G(stats_count)) != IS_UNDEF) {
        zval_ptr_dtor(&BPROF_G(stats_count));
    }

    array_init(&BPROF_G(stats_count));

    bp_init_trace_callbacks();
}

/**
 * Cleanup profiler state
 *
 */
void bp_clean_profiler_state()
{
    /* Clear globals */
    if (Z_TYPE(BPROF_G(stats_count)) != IS_UNDEF) {
        zval_ptr_dtor(&BPROF_G(stats_count));
    }

    ZVAL_UNDEF(&BPROF_G(stats_count));

    BPROF_G(entries) = NULL;
    BPROF_G(profiler_level) = 1;
    BPROF_G(ever_enabled) = 0;

    if (BPROF_G(trace_callbacks)) {
        zend_hash_destroy(BPROF_G(trace_callbacks));
        FREE_HASHTABLE(BPROF_G(trace_callbacks));
        BPROF_G(trace_callbacks) = NULL;
    }

    if (BPROF_G(root)) {
        zend_string_release(BPROF_G(root));
        BPROF_G(root) = NULL;
    }

    /* Delete the array storing ignored function names */
    bp_ignored_functions_clear(BPROF_G(ignored_functions));
    BPROF_G(ignored_functions) = NULL;
}

/**
 * Returns formatted function name
 * @param entry bp_entry
 */
size_t bp_get_entry_name(bp_entry_t *entry, char *result_buf, size_t result_len)
{
    size_t len;

    result_len = bpmin(SCRATCH_BUF_LEN, result_len);

    if (entry->rlvl_bprof) {
        len = snprintf(result_buf, result_len, "%s@%d", ZSTR_VAL(entry->name_bprof), entry->rlvl_bprof);
    } else {
        len = snprintf(result_buf, result_len, "%s", ZSTR_VAL(entry->name_bprof));
    }

    return len;
}


/**
 * Build a caller qualified name for a callee.
 *
 * For example, if A() is caller for B(), then it returns "A>>>B".
 * Recursive invokations are denoted with @<n> where n is the recursion
 * depth.
 *
 * For example, "foo>>>foo@1", and "foo@2>>>foo@3" are examples of direct
 * recursion. And  "bar>>>foo@1" is an example of an indirect recursive
 * call to foo (implying the foo() is on the call stack some levels
 * above).
 *
 */
size_t bp_get_function_stack(bp_entry_t *entry, int level, char *result_buf, size_t result_len)
{
    size_t len = 0;

    /* End recursion if we dont need deeper levels or we dont have any deeper
    * levels */
    if (!entry->prev_bprof || (level <= 1)) {
        return bp_get_entry_name(entry, result_buf, result_len);
    }

    /* Take care of all ancestors first */
    len = bp_get_function_stack(entry->prev_bprof, level - 1, result_buf, result_len);

    /* Append the delimiter */
# define STACK_DELIMITER ">>>"
# define STACK_DELIMITER_LENGTH (sizeof(STACK_DELIMITER) - 1)

    if (result_len < (len + STACK_DELIMITER_LENGTH)) {
        /* Insufficient result_buf. Bail out! */
        return len;
    }

    /* Add delimiter only if entry had ancestors */
    if (len) {
        strncat(result_buf + len, STACK_DELIMITER, result_len - len);
        len += STACK_DELIMITER_LENGTH;
    }

# undef STACK_DELIMITER_LENGTH
# undef STACK_DELIMITER

    /* Append the current function name */
    return len + bp_get_entry_name(entry, result_buf + len, result_len - len);
}

/**
 * Takes an input of the form /a/b/c/d/foo.php and returns
 * a pointer to one-level directory and basefile name
 * (d/foo.php) in the same string.
 */
static const char *bp_get_base_filename(const char *filename)
{
    const char *ptr;
    int found = 0;

    if (!filename)
        return "";

    /* reverse search for "/" and return a ptr to the next char */
    for (ptr = filename + strlen(filename) - 1; ptr >= filename; ptr--) {
        if (*ptr == '/') {
            found++;
        }

        if (found == 2) {
            return ptr + 1;
        }
    }

    /* no "/" char found, so return the whole string */
    return filename;
}

/**
 * Free any items in the free list.
 */
static void bp_free_the_free_list()
{
    bp_entry_t *p = BPROF_G(entry_free_list);
    bp_entry_t *cur;

    while (p) {
        cur = p;
        p = p->prev_bprof;
        free(cur);
    }
}

/**
 * Increment the count of the given stat with the given count
 * If the stat was not set before, inits the stat to the given count
 *
 * @param  zval *counts   Zend hash table pointer
 * @param  char *name     Name of the stat
 * @param  long  count    Value of the stat to incr by
 * @return void
 */
void bp_inc_count(zval *counts, const char *name, zend_long count)
{
    if (!counts) {
        return;
    }

    HashTable *ht = Z_ARRVAL_P(counts);

    if (!ht) {
        return;
    }

    zend_string *key = zend_string_init(name, strlen(name), 0);
    zval *data = zend_hash_find(ht, key);

    if (data) {
        Z_LVAL_P(data) += count;
    } else {
        zval val;
        ZVAL_LONG(&val, count);
        zend_hash_add(ht, key, &val);
    }

    zend_string_release(key);
}

/**
 * Truncates the given timeval to the nearest slot begin, where
 * the slot size is determined by intr
 *
 * @param  tv       Input timeval to be truncated in place
 * @param  intr     Time interval in microsecs - slot width
 * @return void
 */
void bp_trunc_time(struct timeval *tv, zend_ulong intr)
{
    zend_ulong time_in_micro;

    /* Convert to microseconds and truncate that first */
    time_in_micro = (tv->tv_sec * 1000000) + tv->tv_usec;
    time_in_micro /= intr;
    time_in_micro *= intr;

    /* Update tv */
    tv->tv_sec  = (time_in_micro / 1000000);
    tv->tv_usec = (time_in_micro % 1000000);
}

static inline zend_ulong cycle_timer()
{
#if defined(__APPLE__) && defined(__MACH__)
    // On macOS, use the Mach absolute time, accounting for a predefined timebase conversion.
    return mach_absolute_time() / BPROF_G(timebase_conversion);

#elif defined(ZEND_WIN32)
    // On Windows, use the QueryPerformanceCounter API.
    LARGE_INTEGER lt;
    QueryPerformanceCounter(&lt);

    // Convert the counter value to microseconds.
    lt.QuadPart *= 1000000;
    lt.QuadPart /= performance_frequency.QuadPart;

    return lt.QuadPart;

#else
    // For other platforms, use the POSIX clock_gettime with the monotonic clock.
    struct timespec s;
    clock_gettime(CLOCK_MONOTONIC, &s);

    // Convert the time to microseconds.
    return (s.tv_sec * 1000000) + (s.tv_nsec / 1000);
#endif
}
/**
 * Get the current real CPU clock timer
 */
static zend_ulong cpu_timer()
{
#if defined(CLOCK_PROCESS_CPUTIME_ID)
    struct timespec s;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &s);
    return (s.tv_sec * 1000000) + (s.tv_nsec / 1000);
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ((ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000000) + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec);
#endif
}

/**
 * Incr time with the given microseconds.
 */
static void incr_us_interval(struct timeval *start, zend_ulong incr)
{
    zend_ulong total_microseconds = (start->tv_sec * 1000000) + start->tv_usec + incr;
    start->tv_sec  = total_microseconds / 1000000;
    start->tv_usec = total_microseconds % 1000000;
}

/**
 * BPROF_MODE_HIERARCHICAL's begin function callback
 */
void bp_mode_hier_begin_fn_cb(bp_entry_t **entries, bp_entry_t  *current)
{
    /* Get start tsc counter */
    current->tsc_start = cycle_timer();

    /* Get CPU usage */
    if (BPROF_G(bprof_flags) & BPROF_FLAGS_CPU) {
        current->cpu_start = cpu_timer();
    }

    /* Get memory usage */
    if (BPROF_G(bprof_flags) & BPROF_FLAGS_MEMORY) {
        current->mu_start_bprof  = zend_memory_usage(0);
        current->pmu_start_bprof = zend_memory_peak_usage(0);
    }
}

void bp_mode_hier_end_fn_cb(bp_entry_t **entries)
{
    bp_entry_t *top = (*entries);
    char symbol[SCRATCH_BUF_LEN];
    double wt, cpu;

#if PHP_VERSION_ID >= 80000
    if (top->is_trace == 0) {
        BPROF_G(func_hash_counters[top->hash_code])--;
        return;
    }
#endif

    /* Get end tsc counter */
    wt = cycle_timer() - top->tsc_start;

    /* Get the stat array */
    size_t symbol_len = bp_get_function_stack(top, 2, symbol, sizeof(symbol));

    zval *counts = zend_hash_str_find(Z_ARRVAL(BPROF_G(stats_count)), symbol, symbol_len);

    if (counts == NULL) {
        zval count_val;
        array_init(&count_val);
        counts = zend_hash_str_update(Z_ARRVAL(BPROF_G(stats_count)), symbol, symbol_len, &count_val);
    }

    /* Bump stats in the counts hashtable */
    bp_inc_count(counts, "ct", 1);
    bp_inc_count(counts, "wt", (zend_long) wt);

    if (BPROF_G(bprof_flags) & BPROF_FLAGS_CPU) {
        cpu = cpu_timer() - top->cpu_start;

        /* Bump CPU stats in the counts hashtable */
        bp_inc_count(counts, "cpu", (zend_long) cpu);
    }

    if (BPROF_G(bprof_flags) & BPROF_FLAGS_MEMORY) {
        /* Get Memory usage */
        zend_ulong mu_end = zend_memory_usage(0);
        zend_ulong pmu_end = zend_memory_peak_usage(0);

        /* Bump Memory stats in the counts hashtable */
        bp_inc_count(counts, "mu", (zend_long) (mu_end - top->mu_start_bprof));
        bp_inc_count(counts, "pmu", (zend_long) (pmu_end - top->pmu_start_bprof));
    }

    BPROF_G(func_hash_counters[top->hash_code])--;
}

/**
 * bprof enable replaced the zend_execute function with this
 * new execute function. We can do whatever profiling we need to
 * before and after calling the actual zend_execute().
 */

#if PHP_VERSION_ID >= 80000
static void tracer_observer_begin(zend_execute_data *execute_data) {
#if PHP_VERSION_ID >= 80200
    if (execute_data->func->type == ZEND_INTERNAL_FUNCTION) {
        return;
    }
#endif

    begin_profiling(NULL, execute_data);
}

static void tracer_observer_end(zend_execute_data *execute_data, zval *return_value) {
    if (BPROF_G(entries)) {
#if PHP_VERSION_ID >= 80200
        if (execute_data->func->type == ZEND_INTERNAL_FUNCTION) {
            return;
        }
#endif

        end_profiling();
    }
}

static zend_observer_fcall_handlers tracer_observer(zend_execute_data *execute_data) {
    zend_observer_fcall_handlers handlers = {NULL, NULL};
    if (!BPROF_G(enabled)) {
        return handlers;
    }

    if (!execute_data->func || !execute_data->func->common.function_name) {
        return handlers;
    }

    handlers.begin = tracer_observer_begin;
    handlers.end = tracer_observer_end;
    return handlers;
}
#else
ZEND_DLEXPORT void bp_execute_ex (zend_execute_data *execute_data)
{
    int is_profiling = 1;

    if (!BPROF_G(enabled)) {
        _zend_execute_ex(execute_data);
        return;
    }

    //zend_execute_data *real_execute_data = execute_data->prev_execute_data;

    is_profiling = begin_profiling(NULL, execute_data);

    _zend_execute_ex(execute_data);

    if (is_profiling == 1 && BPROF_G(entries)) {
        end_profiling();
    }
}
#endif

/**
 * Very similar to bp_execute. Proxy for zend_execute_internal().
 * Applies to zend builtin functions.
 */

ZEND_DLEXPORT void bp_execute_internal(zend_execute_data *execute_data, zval *return_value)
{
    int is_profiling = 1;

    if (!BPROF_G(enabled) || (BPROF_G(bprof_flags) & BPROF_FLAGS_NO_BUILTINS) > 0) {
        execute_internal(execute_data, return_value);
        return;
    }

    is_profiling = begin_profiling(NULL, execute_data);

    if (!_zend_execute_internal) {
        /* no old override to begin with. so invoke the builtin's implementation  */
        execute_internal(execute_data, return_value);
    } else {
        /* call the old override */
        _zend_execute_internal(execute_data, return_value);
    }

    if (is_profiling == 1 && BPROF_G(entries)) {
        end_profiling();
    }
}

/**
 * Proxy for zend_compile_file(). Used to profile PHP compilation time.
 */
ZEND_DLEXPORT zend_op_array* bp_compile_file(zend_file_handle *file_handle, int type)
{
    int is_profiling = 1;

    if (!BPROF_G(enabled)) {
        return _zend_compile_file(file_handle, type);
    }

    const char *filename;
    zend_string *function_name;
    zend_op_array *op_array;

#if PHP_VERSION_ID < 80100
    filename = bp_get_base_filename(file_handle->filename);
#else
    filename = bp_get_base_filename(ZSTR_VAL(file_handle->filename));
#endif

    function_name = strpprintf(0, "load::%s", filename);

    is_profiling = begin_profiling(function_name, NULL);
    op_array = _zend_compile_file(file_handle, type);

    if (is_profiling == 1 && BPROF_G(entries)) {
        end_profiling();
    }

    zend_string_release(function_name);

    return op_array;
}

/**
 * Proxy for zend_compile_string(). Used to profile PHP eval compilation time.
 */
#if PHP_VERSION_ID < 80000
ZEND_DLEXPORT zend_op_array* bp_compile_string(zval *source_string, char *filename)
#elif PHP_VERSION_ID >= 80200
ZEND_DLEXPORT zend_op_array* bp_compile_string(zend_string *source_string, const char *filename, zend_compile_position position)
#else
ZEND_DLEXPORT zend_op_array* bp_compile_string(zend_string *source_string, const char *filename)
#endif
{
    int is_profiling = 1;

    if (!BPROF_G(enabled)) {
#if PHP_VERSION_ID >= 80200
        return _zend_compile_string(source_string, filename, position);
#else
        return _zend_compile_string(source_string, filename);
#endif
    }

    zend_string *function_name;
    zend_op_array *op_array;

    function_name = strpprintf(0, "eval::%s", filename);

    is_profiling = begin_profiling(function_name, NULL);
#if PHP_VERSION_ID >= 80200
    op_array = _zend_compile_string(source_string, filename, position);
#else
    op_array = _zend_compile_string(source_string, filename);
#endif

    if (is_profiling == 1 && BPROF_G(entries)) {
        end_profiling();
    }

    zend_string_release(function_name);

    return op_array;
}

/**
 * This function gets called once when bprof gets enabled.
 * It replaces all the functions like zend_execute, zend_execute_internal,
 * etc that needs to be instrumented with their corresponding proxies.
 */
static void bp_begin(zend_long bprof_flags)
{
    if (!BPROF_G(enabled)) {
        BPROF_G(enabled)      = 1;
        BPROF_G(bprof_flags) = (uint32)bprof_flags;

        /* Register the appropriate callback functions. */
        BPROF_G(mode_cb).begin_fn_cb = bp_mode_hier_begin_fn_cb;
        BPROF_G(mode_cb).end_fn_cb   = bp_mode_hier_end_fn_cb;

        // One time initializations
        bp_init_profiler_state();

        // start profiling from fictitious main()
        BPROF_G(root) = zend_string_init(ROOT_SYMBOL, sizeof(ROOT_SYMBOL) - 1, 0);

        // start profiling from fictitious main()
        begin_profiling(BPROF_G(root), NULL);
    }
}

/**
 * Called at request shutdown time. Cleans the profiler's global state.
 */
static void bp_end()
{
    /* Bail if not ever enabled */
    if (!BPROF_G(ever_enabled)) {
        return;
    }

    /* Stop profiler if enabled */
    if (BPROF_G(enabled)) {
        bp_stop();
    }

    /* Clean up state */
    bp_clean_profiler_state();
}

/**
 * Called from bprof_disable(). Removes all the proxies setup by bp_begin() and restores the original values.
 */
static void bp_stop()
{
    /* End any unfinished calls */
    while (BPROF_G(entries)) {
        end_profiling();
    }

    /* Stop profiling */
    BPROF_G(enabled) = 0;

    if (BPROF_G(root)) {
        zend_string_release(BPROF_G(root));
        BPROF_G(root) = NULL;
    }
}


///* Free this memory at the end of profiling */
static inline void bp_array_del(zend_string **names)
{
    if (names != NULL) {
        int i = 0;
        for (; names[i] != NULL && i < BPROF_MAX_IGNORED_FUNCTIONS; i++) {
            zend_string_release(names[i]);
            names[i] = NULL;
        }

        efree(names);
    }
}

int bp_pcre_match(zend_string *pattern, const char *str, size_t len, zend_ulong idx)
{
    pcre_cache_entry *pce_regexp;
    zval matches, subparts;

    if (!(pce_regexp = pcre_get_compiled_regex_cache(pattern))) {
        return 0;
    }

    ZVAL_NULL(&subparts);

    zend_string *tmp = zend_string_init(str, len, 0);
    php_pcre_match_impl(pce_regexp, tmp, &matches, &subparts, 0, 0, 0, 0);
    zend_string_release(tmp);

    if (!zend_hash_num_elements(Z_ARRVAL(subparts))) {
        zval_ptr_dtor(&subparts);
        return 0;
    }

    zval_ptr_dtor(&subparts);
    return 1;
}

zend_string *bp_pcre_replace(zend_string *pattern, zend_string *repl, zval *data, int limit)
{
    pcre_cache_entry *pce_regexp;
    zend_string *replace;

    if ((pce_regexp = pcre_get_compiled_regex_cache(pattern)) == NULL) {
        return NULL;
    }

    zend_string *tmp = zval_get_string(data);
    replace = php_pcre_replace_impl(pce_regexp, NULL, ZSTR_VAL(repl), ZSTR_LEN(repl), tmp, limit, 0);
    zend_string_release(tmp);

    return replace;
}

zend_string *bp_trace_callback_sql_query(zend_string *function_name, zend_execute_data *data)
{
    zend_string *trace_name;

    // Check if the function name is "mysqli_query".
    if (strcmp(ZSTR_VAL(function_name), "mysqli_query") == 0) {
        // Get the second argument of the function call.
        zval *arg = ZEND_CALL_ARG(data, 2);

        // Generate trace name using the function name and argument value.
        trace_name = strpprintf(0, "%s#%s", ZSTR_VAL(function_name), Z_STRVAL_P(arg));
    } else {
        // For other function names, get the first argument.
        zval *arg = ZEND_CALL_ARG(data, 1);

        // Generate trace name using the function name and argument value.
        trace_name = strpprintf(0, "%s#%s", ZSTR_VAL(function_name), Z_STRVAL_P(arg));
    }

    return trace_name;  // Return the generated trace name.
}

zend_string* concat_from_hash(zval *array, const char *join_str) {
    if (!array) {
#if BPROF_DEBUG == true
        php_error(E_WARNING, "concat_from_hash: Input array is NULL");
#endif
        return zend_string_init("error", strlen("error"), 0);
    }

    if (Z_TYPE_P(array) != IS_ARRAY) {
#if BPROF_DEBUG == true
        php_error(E_WARNING, "concat_from_hash: Input is not an array");
#endif
        return zend_string_init("error", strlen("error"), 0);
    }

    HashTable *ht = Z_ARRVAL_P(array);
    smart_str result = {0};
    zval *entry;
    int isFirst = 1;

    ZEND_HASH_FOREACH_VAL(ht, entry) {
        if (!entry) {
#if BPROF_DEBUG == true
                php_error(E_WARNING, "concat_from_hash: Encountered NULL entry in hash");
#endif
            continue;
        }

        if (Z_TYPE_P(entry) == IS_STRING) {
            if (!isFirst) {
                smart_str_appends(&result, join_str);
            } else {
                isFirst = 0;
            }
            smart_str_appends(&result, Z_STRVAL_P(entry));
        }
    } ZEND_HASH_FOREACH_END();

    if (!result.s) {
#if BPROF_DEBUG == true
            php_error(E_WARNING, "concat_from_hash: Resulting smart_str is NULL");
#endif
        return zend_string_init("error", strlen("error"), 0);
    }

    smart_str_0(&result);
    return result.s;
}


zend_string *bp_trace_callback_predis(zend_string *symbol, zend_execute_data *data) {
    zend_string *result;
    zval funcId, funcArgs, retvalcommand, retvalargs;
    zval *arg = ZEND_CALL_ARG(data, 1);

    // Debug: Check if arg is NULL
    if (!arg) {
#if BPROF_DEBUG == true
            php_error(E_WARNING, "bp_trace_callback_predis: arg is NULL");
#endif
        return strpprintf(0, "%s#%s", ZSTR_VAL(symbol), "arg_is_null");
    }

    // Initialize and call 'getId' method
    ZVAL_STRING(&funcId, "getId");
    zend_fcall_info fciId = {
        sizeof(fciId),
        funcId,
        &retvalcommand,
        NULL,
        Z_OBJ_P(arg),
        0,
        NULL
    };
    if (zend_call_function(&fciId, NULL) == FAILURE) {
        zval_dtor(&funcId);
#if BPROF_DEBUG == true
            php_error(E_WARNING, "bp_trace_callback_predis: zend_call_function for getId failed");
#endif
        return strpprintf(0, "%s#%s", ZSTR_VAL(symbol), "unknown");
    }
    zval_dtor(&funcId);

    // Initialize and call 'getArguments' method
    ZVAL_STRING(&funcArgs, "getArguments");
    zend_fcall_info fciArgs = {
        sizeof(fciArgs),
        funcArgs,
        &retvalargs,
        NULL,
        Z_OBJ_P(arg),
        0,
        NULL
    };
    if (zend_call_function(&fciArgs, NULL) == FAILURE) {
        zval_dtor(&funcArgs);
#if BPROF_DEBUG == true
            php_error(E_WARNING, "bp_trace_callback_predis: zend_call_function for getArguments failed");
#endif
        return strpprintf(0, "%s#%s", ZSTR_VAL(symbol), "unknown");
    }
    zval_dtor(&funcArgs);

    // Debug: Check if retvalargs is a valid hash
    if (Z_TYPE_P(&retvalargs) != IS_ARRAY) {
#if BPROF_DEBUG == true
            php_error(E_WARNING, "bp_trace_callback_predis: retvalargs is not an array");
#endif
        return strpprintf(0, "%s#%s", ZSTR_VAL(symbol), "invalid_args");
    }

    // Concatenate results
   zend_string *argsConcatenated = concat_from_hash(&retvalargs, "\n");
   result = strpprintf(0, "%s#%s %s", ZSTR_VAL(symbol), Z_STRVAL(retvalcommand), ZSTR_VAL(argsConcatenated));


    // Release the temporary zend_string from concat_from_hash
    zend_string_release(argsConcatenated);

    return result;
}

zend_string *bp_trace_callback_pdo_statement_execute(zend_string *symbol, zend_execute_data *data)
{
    zend_string *result = NULL;
    zend_class_entry *pdo_ce;
    zval *query_string = NULL;
    zval *object = (data->This.value.obj) ? &(data->This) : NULL;

    if (object) {
        // Get the PDO class entry.
        pdo_ce = Z_OBJCE_P(object);

        // Read the "queryString" property from the PDO object.
        #if PHP_VERSION_ID < 80000
        query_string = zend_read_property(pdo_ce, object, "queryString", sizeof("queryString") - 1, 0, NULL);
        #else
        query_string = zend_read_property(pdo_ce, Z_OBJ_P(object), "queryString", sizeof("queryString") - 1, 0, NULL);
        #endif

        // Check if the query_string property exists and is a string.
        if (query_string != NULL && Z_TYPE_P(query_string) == IS_STRING) {
            result = strpprintf(0, "%s#%s", ZSTR_VAL(symbol), Z_STRVAL_P(query_string));
        }
    }

    if (result == NULL) {
        result = zend_string_copy(symbol);  // Return the original symbol as trace name.
    }

    return result;
}



zend_string *bp_trace_callback_curl_exec(zend_string *symbol, zend_execute_data *data)
{
    zend_string *result;
    zval func, retval, *option;
    zval *arg = ZEND_CALL_ARG(data, 1);

#if PHP_VERSION_ID < 80000
    if (arg == NULL || Z_TYPE_P(arg) != IS_RESOURCE) {
#else
        if (arg == NULL || Z_TYPE_P(arg) != IS_OBJECT) {
#endif
        result = strpprintf(0, "%s", ZSTR_VAL(symbol));
        return result;
    }

    zval params[1];
    ZVAL_COPY(&params[0], arg);
    ZVAL_STRING(&func, "curl_getinfo");

    zend_fcall_info fci = {
            sizeof(fci),
#if PHP_VERSION_ID < 70100
            EG(function_table),
#endif
            func,
#if PHP_VERSION_ID < 70100
            NULL,
#endif
            &retval,
            params,
            NULL,
#if PHP_VERSION_ID < 80000
            1,
#endif
            1
    };

    if (zend_call_function(&fci, NULL) == FAILURE) {
        result = strpprintf(0, "%s#%s", ZSTR_VAL(symbol), "unknown");
    } else {
        option = zend_hash_str_find(Z_ARRVAL(retval), "url", sizeof("url") - 1);
        result = strpprintf(0, "%s#%s", ZSTR_VAL(symbol), Z_STRVAL_P(option));
    }

    zval_ptr_dtor(&func);
    zval_ptr_dtor(&retval);
    zval_ptr_dtor(&params[0]);

    return result;
}

zend_string *bp_trace_callback_exec(zend_string *function_name, zend_execute_data *data)
{
    zend_string *trace_name;
    zval *arg = ZEND_CALL_ARG(data, 1);
    trace_name = strpprintf(0, "%s#%s", ZSTR_VAL(function_name), Z_STRVAL_P(arg));
    return trace_name;
}

static inline void bp_free_trace_callbacks(zval *val) {
    efree(Z_PTR_P(val));
}


void bp_init_trace_callbacks()
{
    bp_trace_callback callback;

    if (BPROF_G(trace_callbacks)) {
        return;
    }

    BPROF_G(trace_callbacks) = NULL;
    ALLOC_HASHTABLE(BPROF_G(trace_callbacks));

    if (!BPROF_G(trace_callbacks)) {
        return;
    }

    zend_hash_init(BPROF_G(trace_callbacks), 8, NULL, bp_free_trace_callbacks, 0);

    callback = bp_trace_callback_sql_query;
    register_trace_callback("PDO::exec", callback);
    register_trace_callback("PDO::query", callback);
    register_trace_callback("mysql_query", callback);
    register_trace_callback("mysqli_query", callback);
    register_trace_callback("mysqli::query", callback);

     callback = bp_trace_callback_predis;
     register_trace_callback("Predis\\Client::executeCommand", callback);
    // \Predis\Client::executeCommand(\Predis\Command\CommandInterface $a);

    callback = bp_trace_callback_pdo_statement_execute;
    register_trace_callback("PDOStatement::execute", callback);

    callback = bp_trace_callback_curl_exec;
    register_trace_callback("curl_exec", callback);

    callback = bp_trace_callback_exec;
    register_trace_callback("exec", callback);
    register_trace_callback("system", callback);
    register_trace_callback("popen", callback);
    register_trace_callback("pcntl_exec", callback);
    register_trace_callback("shell_exec", callback);
    register_trace_callback("proc_open", callback);
    register_trace_callback("eval", callback);
    register_trace_callback("passthru", callback);
    register_trace_callback("fopen", callback);
    register_trace_callback("file_get_contents", callback);
    register_trace_callback("file_put_contents", callback);
    register_trace_callback("fopen", callback);
    register_trace_callback("preg_match", callback);
}
