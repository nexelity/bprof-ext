#ifndef BPROF_TRACE_H
#define BPROF_TRACE_H

static zend_always_inline void bp_mode_common_beginfn(bp_entry_t **entries, bp_entry_t *current)
{
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

static zend_always_inline int bp_ignored_functions_filter_collision(bp_ignored_functions *functions, zend_ulong hash)
{
    zend_ulong idx = hash % BPROF_MAX_IGNORED_FUNCTIONS;
    return functions->filter[idx];
}

static zend_always_inline int bp_ignore_entry_work(zend_ulong hash_code, zend_string *curr_func)
{
    if (BPROF_G(ignored_functions) == NULL) {
        return 0;
    }

    bp_ignored_functions *functions = BPROF_G(ignored_functions);

    if (bp_ignored_functions_filter_collision(functions, hash_code)) {
        int i = 0;
        for (; functions->names[i] != NULL; i++) {
            zend_string *name = functions->names[i];
            if (zend_string_equals(curr_func, name)) {
                return 1;
            }
        }
    }

    return 0;
}

static zend_always_inline zend_string *bp_get_function_name(zend_execute_data *execute_data)
{
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
        real_function_name = strpprintf(0, "%s::%s", curr_func->common.scope->name->val, ZSTR_VAL(curr_func->common.function_name));
    } else {
        real_function_name = zend_string_copy(curr_func->common.function_name);
    }

    return real_function_name;
}

static zend_always_inline zend_string *bp_get_trace_callback(zend_string *function_name, zend_execute_data *data)
{
    zend_string *trace_name;
    bp_trace_callback *callback;

    if (BPROF_G(trace_callbacks)) {
        callback = (bp_trace_callback*)zend_hash_find_ptr(BPROF_G(trace_callbacks), function_name);
        if (callback) {
            trace_name = (*callback)(function_name, data);
        } else {
            return function_name;
        }
    } else {
        return function_name;
    }

    zend_string_release(function_name);

    return trace_name;
}

static zend_always_inline bp_entry_t *bp_fast_alloc_bprof_entry()
{
    bp_entry_t *p;

    p = BPROF_G(entry_free_list);

    if (p) {
        BPROF_G(entry_free_list) = p->prev_bprof;
        return p;
    } else {
        return (bp_entry_t *)malloc(sizeof(bp_entry_t));
    }
}

static zend_always_inline void bp_fast_free_bprof_entry(bp_entry_t *p)
{
    if (p->name_bprof != NULL) {
        zend_string_release(p->name_bprof);
    }

    /* we use/overload the prev_bprof field in the structure to link entries in
     * the free list.
     * */
    p->prev_bprof = BPROF_G(entry_free_list);
    BPROF_G(entry_free_list) = p;
}

static zend_always_inline int begin_profiling(zend_string *root_symbol, zend_execute_data *execute_data)
{
    zend_string *function_name;
    bp_entry_t **entries = &BPROF_G(entries);

    if (root_symbol == NULL) {
        function_name = bp_get_function_name(execute_data);
    } else {
        function_name = zend_string_copy(root_symbol);
    }

    if (function_name == NULL) {
        return 0;
    }

    zend_ulong hash_code = ZSTR_HASH(function_name);
    int profile_curr = !bp_ignore_entry_work(hash_code, function_name);
    if (profile_curr) {
        if (execute_data != NULL) {
            function_name = bp_get_trace_callback(function_name, execute_data);
        }

        bp_entry_t *cur_entry = bp_fast_alloc_bprof_entry();
        (cur_entry)->hash_code = hash_code % BPROF_FUNC_HASH_COUNTERS_SIZE;
        (cur_entry)->name_bprof = function_name;
        (cur_entry)->prev_bprof = (*(entries));
#if PHP_VERSION_ID >= 80000
        (cur_entry)->is_trace = 1;
#endif
        /* Call the universal callback */
        bp_mode_common_beginfn((entries), (cur_entry));
        /* Call the mode's beginfn callback */
        BPROF_G(mode_cb).begin_fn_cb((entries), (cur_entry));
        /* Update entries linked list */
        (*(entries)) = (cur_entry);
    } else {
#if PHP_VERSION_ID >= 80000
        bp_entry_t *cur_entry = bp_fast_alloc_bprof_entry();
        (cur_entry)->name_bprof = zend_string_copy((*(entries))->name_bprof);
        (cur_entry)->prev_bprof = (*(entries));
        (cur_entry)->is_trace = 0;
        (*(entries)) = (cur_entry);
#endif
        zend_string_release(function_name);
    }

    return profile_curr;
}

static zend_always_inline void end_profiling()
{
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
#endif