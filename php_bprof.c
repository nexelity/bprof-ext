#include "main/php.h"
#include "Zend/zend_smart_str.h"
#include "Zend/zend_API.h"
#include "ext/standard/info.h"
#include "php_bprof.h"
#include <sys/resource.h>
#include <stdlib.h>

#if __APPLE__

#include <mach/mach_time.h>
#include <Zend/zend_constants.h>
#include <Zend/zend_ini.h>
#include <Zend/zend_observer.h>

#endif

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

/**
 * Start bprof profiling in hierarchical mode.
 *
 * @param  long $flags  flags for hierarchical mode
 * @return void
 */
PHP_FUNCTION (bprof_enable) {
    zend_long bprof_flags = 0; // Initialize bprof flags
    zval *optional_array = NULL; // Optional array for future use

    // Parse input parameters
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "|lz", &bprof_flags, &optional_array) == FAILURE) {
        return; // Return early on parameter parsing failure
    }

    // Begin profiling with specified flags
    bp_begin(bprof_flags);
}

/**
 * Stops bprof from profiling in hierarchical mode anymore and returns the
 * profile info.
 *
 * @param  void
 * @return array  hash-array of profile info
 */
PHP_FUNCTION (bprof_disable) {
    if (BPROF_G(enabled)) {
        bp_stop();
        RETURN_ZVAL(&BPROF_G(stats_count), 1, 0);
    }
}

static void php_bprof_init_globals(zend_bprof_globals *globals) {
    globals->enabled = 0;
    globals->ever_enabled = 0;
    globals->bprof_flags = 0;
    globals->entries = NULL;
    globals->root = NULL;
    globals->trace_callbacks = NULL;

    ZVAL_UNDEF(&globals->stats_count);

    /* no free bp_entry_t structures to start with */
    globals->entry_free_list = NULL;

    for (int i = 0; i < BPROF_FUNC_HASH_COUNTERS_SIZE; i++) {
        globals->func_hash_counters[i] = 0;
    }
}

/**
 * Module init callback.
 */
PHP_MINIT_FUNCTION (bprof) {
    ZEND_INIT_MODULE_GLOBALS(bprof, php_bprof_init_globals, NULL)

    bp_register_constants(INIT_FUNC_ARGS_PASSTHRU);

    zend_observer_fcall_register(tracer_observer);

    /* Replace zend_execute_internal with our proxy */
    _zend_execute_internal = zend_execute_internal;
    zend_execute_internal = bp_execute_internal;

    return SUCCESS;
}

/**
 * Module shutdown callback.
 */
PHP_MSHUTDOWN_FUNCTION (bprof) {
    /* free any remaining items in the free list */
    bp_free_the_free_list();
    zend_execute_internal = _zend_execute_internal;

    return SUCCESS;
}

/**
 * Request init callback. Nothing to do yet!
 */
PHP_RINIT_FUNCTION (bprof) {
    BPROF_G(timebase_conversion) = get_timebase_conversion();
    return SUCCESS;
}

/**
 * Request shutdown callback. Stop profiling and return.
 */
PHP_RSHUTDOWN_FUNCTION (bprof) {
    bp_end();
    return SUCCESS;
}

/**
 * Module info callback. Returns the bprof version.
 */
PHP_MINFO_FUNCTION (bprof) {
    php_info_print_table_start();
    php_info_print_table_row(2, "Author", "Ben Poulson <ben.poulson@nexelity.com>");
    php_info_print_table_row(2, "Version", BPROF_VERSION);
    php_info_print_table_end();
    // Display any INI entries defined for your extension
    DISPLAY_INI_ENTRIES();
}

static void bp_register_constants(INIT_FUNC_ARGS) {
    REGISTER_LONG_CONSTANT("BPROF_FLAGS_NO_BUILTINS", BPROF_FLAGS_NO_BUILTINS, CONST_CS | CONST_PERSISTENT);
}

double get_timebase_conversion() {
#if defined(__APPLE__)
    mach_timebase_info_data_t info;
    (void) mach_timebase_info(&info);

    return info.denom * 1000. / info.numer;
#else
    return 1.0;
#endif
}

/**
 * Initialize profiler state
 *
 */
void bp_init_profiler_state() {
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
void bp_clean_profiler_state() {
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
}

size_t bp_get_entry_name(bp_entry_t *entry, char *result_buf, size_t result_len) {
    if (result_len == 0 || !entry) {
        return 0;
    }

    int len = entry->rlvl_bprof
              ? snprintf(result_buf, result_len, "%s@%d", entry->name_bprof->val, entry->rlvl_bprof)
              : snprintf(result_buf, result_len, "%s", entry->name_bprof->val);

    if (len < 0 || (size_t) len >= result_len) {
        result_buf[result_len - 1] = '\0'; // Ensure null termination
        return result_len - 1;
    }

    return (size_t) len;
}

size_t bp_get_function_stack(bp_entry_t *entry, int level, char *result_buf, size_t result_len) {
    if (result_len == 0 || !entry) {
        return 0;
    }

    size_t len = 0;

    if (entry->prev_bprof && level > 1) {
        len = bp_get_function_stack(entry->prev_bprof, level - 1, result_buf, result_len);
        if (len >= result_len - 1) {
            return len; // Buffer is full
        }

        const char *stack_delimiter = ">>>";
        size_t delimiter_len = strlen(stack_delimiter);

        if (result_len - len > delimiter_len) {
            memcpy(result_buf + len, stack_delimiter, delimiter_len);
            len += delimiter_len;
        } else {
            return len; // No space for delimiter
        }
    }

    if (result_len - len > 0) {
        size_t added_len = bp_get_entry_name(entry, result_buf + len, result_len - len);
        len += added_len;
    }

    return len;
}

/**
 * Free any items in the free list.
 */
static void bp_free_the_free_list() {
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
void bp_inc_count(zval *counts, const char *name, zend_long count) {
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

static inline zend_ulong cycle_timer() {
#if defined(__APPLE__) && defined(__MACH__)
    // On macOS, use the Mach absolute time, accounting for a predefined timebase conversion.
    return mach_absolute_time() / BPROF_G(timebase_conversion);
#else
    // For other platforms, use the POSIX clock_gettime with the monotonic clock.
    struct timespec s;
    clock_gettime(CLOCK_MONOTONIC, &s);

    // Convert the time to microseconds.
    return (s.tv_sec * 1000000) + (s.tv_nsec / 1000);
#endif
}

/**
 * Begins a profiling session for hierarchical mode.
 *
 * @param bp_entry_t **entries The list of profiling entries.
 * @param bp_entry_t *current The current profiling entry.
 */
void bp_begin_function_callback(bp_entry_t **entries, bp_entry_t *current) {
    // Initialize start time for current profile entry
    current->tsc_start = cycle_timer();
}

void bp_end_function_callback(bp_entry_t **entries) {
    // Retrieve the top entry in the profiling stack
    bp_entry_t *top = (*entries);

    // Buffer for storing symbol information
    char symbol[SCRATCH_BUF_LEN];

    // Variables for wall time and CPU time
    zend_ulong wt;

    // Skip processing for non-tracing entries in PHP 8.0 and above
    if (top->is_trace == 0) {
        BPROF_G(func_hash_counters[top->hash_code])--; // Decrement function hash counter
        return; // Early return if not tracing
    }

    // Calculate elapsed wall time
    wt = cycle_timer() - top->tsc_start;

    // Retrieve the symbol name and length for the current entry
    size_t symbol_len = bp_get_function_stack(top, 2, symbol, sizeof(symbol));

    // Find or create a new entry in the statistics hash table
    zval *counts = zend_hash_str_find(Z_ARRVAL(BPROF_G(stats_count)), symbol, symbol_len);
    if (counts == NULL) {
        zval count_val;
        array_init(&count_val);
        counts = zend_hash_str_update(Z_ARRVAL(BPROF_G(stats_count)), symbol, symbol_len, &count_val);
    }

    // Update call count and wall time statistics
    bp_inc_count(counts, "ct", 1);
    bp_inc_count(counts, "wt", (zend_long) wt);


    // Decrement the function hash counter for the current entry
    BPROF_G(func_hash_counters[top->hash_code])--;
}

/**
 * bprof enable replaced the zend_execute function with this
 * new execute function. We can do whatever profiling we need to
 * before and after calling the actual zend_execute().
 */
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


/**
 * Very similar to bp_execute. Proxy for zend_execute_internal().
 * Applies to zend builtin functions.
 */
void bp_execute_internal(zend_execute_data *execute_data, zval *return_value) {
    if (!BPROF_G(enabled) || (BPROF_G(bprof_flags) & BPROF_FLAGS_NO_BUILTINS) > 0) {
        execute_internal(execute_data, return_value);
        return;
    }

    begin_profiling(NULL, execute_data);

    if (!_zend_execute_internal) {
        /* no old override to begin with. so invoke the builtin implementation  */
        execute_internal(execute_data, return_value);
    } else {
        /* call the old override */
        _zend_execute_internal(execute_data, return_value);
    }

    if (BPROF_G(entries)) {
        end_profiling();
    }
}

/**
 * This function gets called once when bprof gets enabled.
 * It replaces all the functions like zend_execute, zend_execute_internal,
 * etc that needs to be instrumented with their corresponding proxies.
 */
static void bp_begin(zend_long bprof_flags) {
    // Check if bprof is already enabled and throw an exception if it is
    if (BPROF_G(enabled)) {
        php_error(E_WARNING, "bprof_begin already active");
        return; // Early return after throwing the exception
    }

    BPROF_G(enabled) = 1; // Enable bprof
    BPROF_G(bprof_flags) = bprof_flags; // Set profiling flags

    // Register callback functions for profiling mode
    BPROF_G(mode_cb).begin_fn_cb = bp_begin_function_callback;
    BPROF_G(mode_cb).end_fn_cb = bp_end_function_callback;

    // Perform one-time initializations of the profiler state
    bp_init_profiler_state();

    // Initialize profiling with a fictitious root symbol
    BPROF_G(root) = zend_string_init(ROOT_SYMBOL, sizeof(ROOT_SYMBOL) - 1, 0);

    // Begin profiling from the fictitious main function
    begin_profiling(BPROF_G(root), NULL);
}

/**
 * Called at request shutdown time. Cleans the profiler's global state.
 */
static void bp_end() {
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
static void bp_stop() {
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

zend_string *bp_trace_callback_sql_query(zend_string *function_name, zend_execute_data *data) {
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

zend_string *concat_from_hash(zval *array, const char *join_str) {
    if (!array) {
        return zend_string_init("error", strlen("error"), 0);
    }

    if (Z_TYPE_P(array) != IS_ARRAY) {
        return zend_string_init("error", strlen("error"), 0);
    }

    HashTable *ht = Z_ARRVAL_P(array);
    smart_str result = {0};
    zval *entry;
    int isFirst = 1;

    ZEND_HASH_FOREACH_VAL(ht, entry)
            {
                if (!entry) {
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
            }ZEND_HASH_FOREACH_END();

    if (!result.s) {
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
        return strpprintf(0, "%s#%s", ZSTR_VAL(symbol), "unknown");
    }
    zval_dtor(&funcArgs);

    // Debug: Check if retvalargs is a valid hash
    if (Z_TYPE_P(&retvalargs) != IS_ARRAY) {
        return strpprintf(0, "%s#%s", ZSTR_VAL(symbol), "invalid_args");
    }

    // Concatenate results
    zend_string *argsConcatenated = concat_from_hash(&retvalargs, "\n");
    result = strpprintf(0, "%s#%s %s", ZSTR_VAL(symbol), Z_STRVAL(retvalcommand), ZSTR_VAL(argsConcatenated));


    // Release the temporary zend_string from concat_from_hash
    zend_string_release(argsConcatenated);

    return result;
}

zend_string *bp_trace_callback_pdo_statement_execute(zend_string *symbol, zend_execute_data *data) {
    zend_string *result = NULL;
    zend_class_entry *pdo_ce;
    zval *query_string = NULL;
    zval *object = (data->This.value.obj) ? &(data->This) : NULL;

    if (object) {
        // Get the PDO class entry.
        pdo_ce = Z_OBJCE_P(object);

        // Read the "queryString" property from the PDO object.
        query_string = zend_read_property(pdo_ce, Z_OBJ_P(object), "queryString", sizeof("queryString") - 1, 0, NULL);

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


zend_string *bp_trace_callback_curl_exec(zend_string *symbol, zend_execute_data *data) {
    zend_string *result;
    zval func, retval, *option;
    zval *arg = ZEND_CALL_ARG(data, 1);

    if (arg == NULL || Z_TYPE_P(arg) != IS_OBJECT) {
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

zend_string *bp_trace_callback_exec(zend_string *function_name, zend_execute_data *data) {
    zend_string *trace_name;
    zval *arg = ZEND_CALL_ARG(data, 1);
    trace_name = strpprintf(0, "%s#%s", ZSTR_VAL(function_name), Z_STRVAL_P(arg));
    return trace_name;
}

static inline void bp_free_trace_callbacks(zval *val) {
    efree(Z_PTR_P(val));
}


void bp_init_trace_callbacks() {
    if (BPROF_G(trace_callbacks)) {
        return;
    }

    BPROF_G(trace_callbacks) = NULL;
    ALLOC_HASHTABLE(BPROF_G(trace_callbacks));

    if (!BPROF_G(trace_callbacks)) {
        return;
    }

    zend_hash_init(BPROF_G(trace_callbacks), 8, NULL, bp_free_trace_callbacks, 0);

    bp_trace_callback callback;

    callback = bp_trace_callback_sql_query;
    register_trace_callback("PDO::exec", callback)
    register_trace_callback("PDO::query", callback)
    register_trace_callback("mysql_query", callback)
    register_trace_callback("mysqli_query", callback)
    register_trace_callback("mysqli::query", callback)

    callback = bp_trace_callback_predis;
    register_trace_callback("Predis\\Client::executeCommand", callback)

    callback = bp_trace_callback_pdo_statement_execute;
    register_trace_callback("PDOStatement::execute", callback)

    callback = bp_trace_callback_curl_exec;
    register_trace_callback("curl_exec", callback)

    callback = bp_trace_callback_exec;
    register_trace_callback("exec", callback)
    register_trace_callback("system", callback)
    register_trace_callback("popen", callback)
    register_trace_callback("pcntl_exec", callback)
    register_trace_callback("shell_exec", callback)
    register_trace_callback("proc_open", callback)
    register_trace_callback("eval", callback)
    register_trace_callback("passthru", callback)
    register_trace_callback("fopen", callback)
    register_trace_callback("file_get_contents", callback)
    register_trace_callback("file_put_contents", callback)
    register_trace_callback("fopen", callback)
    register_trace_callback("preg_match", callback)
}

static zend_always_inline void bp_mode_common_begin_function(bp_entry_t **entries, bp_entry_t *current) {
    bp_entry_t *p;

    /* This symbol's recursive level */
    int recurse_level = 0;

    if (BPROF_G(func_hash_counters[current->hash_code]) > 0) {
        /* Find this symbols recurse level */
        for (p = (*entries); p; p = p->prev_bprof) {
            if (zend_string_equals(current->name_bprof, p->name_bprof)) {
                recurse_level = (p->rlvl_bprof) + 1;
                break;
            }
        }
    }

    BPROF_G(func_hash_counters[current->hash_code])++;

    /* Init current function's recurse level */
    current->rlvl_bprof = recurse_level;
}

static zend_always_inline zend_string *bp_get_function_name(zend_execute_data *execute_data) {
    zend_function *curr_func;
    zend_string *real_function_name;

    if (!execute_data) {
        return NULL;
    }

    curr_func = execute_data->func;

    if (!curr_func->common.function_name) {
        return NULL;
    }

    if (curr_func->common.scope != NULL) {
        real_function_name = strpprintf(0, "%s::%s", curr_func->common.scope->name->val,
                                        ZSTR_VAL(curr_func->common.function_name));
    } else {
        real_function_name = zend_string_copy(curr_func->common.function_name);
    }

    return real_function_name;
}

static zend_always_inline bp_entry_t *bp_fast_alloc_bprof_entry() {
    bp_entry_t *p;

    p = BPROF_G(entry_free_list);

    if (p) {
        BPROF_G(entry_free_list) = p->prev_bprof;
        return p;
    } else {
        return (bp_entry_t *) malloc(sizeof(bp_entry_t));
    }
}

static zend_always_inline void bp_fast_free_bprof_entry(bp_entry_t *p) {
    if (p->name_bprof != NULL) {
        zend_string_release(p->name_bprof);
    }

    /* we use/overload the prev_bprof field in the structure to link entries in
     * the free list.
     * */
    p->prev_bprof = BPROF_G(entry_free_list);
    BPROF_G(entry_free_list) = p;
}

static zend_always_inline void begin_profiling(zend_string *root_symbol, zend_execute_data *execute_data) {
    zend_string *function_name;
    bp_entry_t **entries = &BPROF_G(entries);

    if (root_symbol == NULL) {
        function_name = bp_get_function_name(execute_data);
    } else {
        function_name = zend_string_copy(root_symbol);
    }

    if (function_name == NULL) {
        return;
    }

    zend_ulong hash_code = ZSTR_HASH(function_name);
    bp_entry_t *cur_entry = bp_fast_alloc_bprof_entry();
    (cur_entry)->hash_code = hash_code % BPROF_FUNC_HASH_COUNTERS_SIZE;
    (cur_entry)->name_bprof = function_name;
    (cur_entry)->prev_bprof = (*(entries));
    (cur_entry)->is_trace = 1;

    /* Call the universal callback */
    bp_mode_common_begin_function((entries), (cur_entry));
    /* Call the mode's beginfn callback */
    BPROF_G(mode_cb).begin_fn_cb((entries), (cur_entry));
    /* Update entries linked list */
    (*(entries)) = (cur_entry);
}

static zend_always_inline void end_profiling() {
    bp_entry_t *cur_entry;
    bp_entry_t **entries = &BPROF_G(entries);

    /* Call the mode's endfn callback. */
    /* NOTE(cjiang): we want to call this 'end_fn_cb' before */
    /* 'hp_mode_common_endfn' to avoid including the time in */
    /* 'hp_mode_common_endfn' in the profiling results.      */
    BPROF_G(mode_cb).end_fn_cb(entries);
    cur_entry = (*(entries));
    /* Free top entry and update entries linked list */
    (*(entries)) = (*(entries))->prev_bprof;
    bp_fast_free_bprof_entry(cur_entry);
}
