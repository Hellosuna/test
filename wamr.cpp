#include "postgres.h"
//#include "knl/knl_variable.h"
#include "utils/builtins.h"
#include "access/hash.h"
#include "miscadmin.h"
#include "funcapi.h"
#include "executor/spi.h"
#include<string>
//#include <string.h>
#include <vector>
#include <map>
#include<errno.h>
#include "wasm_export.h"
#include "bh_read_file.h"
#include "bh_getopt.h"
PG_MODULE_MAGIC;

extern "C" Datum wasm_create_instance(PG_FUNCTION_ARGS);
extern "C" Datum wasm_drop_instance(PG_FUNCTION_ARGS);
extern "C" Datum wasm_get_instances(PG_FUNCTION_ARGS);
extern "C" Datum wasm_get_exported_functions(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_int8_0(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_int8_1(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_int8_2(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_int8_3(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_int8_4(PG_FUNCTION_ARGS);
extern "C" Datum wasm_invoke_function_int8_5(PG_FUNCTION_ARGS);
//extern "C" Datum wasm_invoke_function_text_1(PG_FUNCTION_ARGS);
//extern "C" Datum wasm_invoke_function_text_2(PG_FUNCTION_ARGS);

//static const char * const malloc_func = "opengauss_malloc";
//static const char OPENGAUSS_TEXT = 3;
//typedef int errno_t;
typedef struct WasmVM {
    wasm_module_inst_t module_inst;
    std::string wasm_file;
} WasmVM;

typedef struct TupleInstanceState {
    TupleDesc tupd;
    std::map<int64, WasmVM*>::iterator currindex;
    std::map<int64, WasmVM*>::iterator lastindex;
} TupleInstanceState;

typedef struct WasmFuncInfo {
    std::string funcname;
    std::vector<std::string> inputs;
    std::string outputs;
} WasmFuncInfo;

typedef struct TupleFuncState {
    TupleDesc tupd;
    std::vector<WasmFuncInfo*>::iterator currindex;
    std::vector<WasmFuncInfo*>::iterator lastindex;
} TupleFuncState;

#define BUF_LEN 256
#define MAX_PARAMS 5
#define MAX_RETURNS 1

// Store the wasm vm  info globally 
static std::map<int64, WasmVM*> instances; 

// Store the wasm exported function info globally
static std::map<int64, std::vector<WasmFuncInfo *>*> exported_functions;

static WasmVM* find_wasm_vm(int64 instanceid)
{
    std::map<int64, WasmVM*>::iterator itor = instances.begin();
    while (itor != instances.end()) {
        if (itor->first == instanceid) {
            return itor->second;
        }
        itor++;
    }
    // elog(DEBUG1, "wasm_engine: not find instance info for instanceid %ld", instanceid);
    return NULL;
}

static std::vector<WasmFuncInfo*>* find_exported_func_list(int64 instanceid)
{
    std::map<int64, std::vector<WasmFuncInfo*>*>::iterator itor = exported_functions.begin();
    while (itor != exported_functions.end()) {
        if (itor->first == instanceid) {
            return itor->second;
        }
        itor++;
    }
    // elog(DEBUG1, "wasm_engine: not find exported func info for instanceid %ld", instanceid);
    return NULL;
}

static int64 generate_uuid(Datum input) 
{
    Datum uuid = DirectFunctionCall1(hashtext, input);
    return DatumGetInt64(uuid);
}

static int64 wasm_invoke_function(char *instanceid_str, char* funcname, std::vector<int64> &args)
{
    int64 instanceid = atol(instanceid_str);
    uint32 stack_size = 8092;
    wasm_exec_env_t exec_env = NULL;
    uint32_t wasm_buffer = 0;

    WasmVM* wasmVM = find_wasm_vm(instanceid);
    wasm_function_inst_t func = NULL;
    if (wasmVM == NULL) {
        ereport(ERROR, (errmsg("wasm_engine: instance with id %ld is not find", instanceid)));
    }

    wasm_val_t params[args.size()];
    for (unsigned int i = 0; i < args.size(); ++i) {
        //params[i] = WasmEdge_ValueGenI64(args[i]);
		params[i].kind = WASM_I64;
		params[i].of.i64 = args[i] ;
    }

    wasm_val_t result[1];
    if (!(func = wasm_runtime_lookup_function(wasmVM->module_inst, funcname,
                                              NULL))) {
        printf("The %s wasm function is not found.\n",funcname);
        return 0;
    }
    exec_env = wasm_runtime_create_exec_env(wasmVM->module_inst, stack_size);
    if (!exec_env) {
        printf("Create wasm execution environment failed.\n");
        return 0;
    }
    // pass 4 elements for function arguments
    if (!wasm_runtime_call_wasm_a(exec_env, func, 1, result, args.size(), params)) {
        printf("call wasm function generate_float failed. %s\n",
               wasm_runtime_get_exception(wasmVM->module_inst));
        return 0;
    }
    int64 ret_val = 0;
    ret_val = result[0].of.i64;
    /* Resources deallocations. */
    //WasmEdge_StringDelete(wasm_func);

    return ret_val;
}

/*static char* wasm_invoke_function2(char *instanceid_str, char* funcname, std::vector<char*> args)
{
    int64 instanceid = atol(instanceid_str);
    WasmVM* wasmVM = find_wasm_vm(instanceid);
    if (wasmVM == NULL) {
        ereport(ERROR, (errmsg("wasm_engine: instance with id %ld is not find", instanceid)));
    }

    WasmEdge_VMContext *vm_context = wasmVM->vm_context;
    const WasmEdge_ModuleInstanceContext* instance_ctx = WasmEdge_VMGetActiveModule(vm_context);
    WasmEdge_String mem_name = WasmEdge_StringCreateByCString("memory");
    WasmEdge_MemoryInstanceContext* mem_ctx = WasmEdge_ModuleInstanceFindMemory(instance_ctx, mem_name);
    WasmEdge_StringDelete(mem_name);

    WasmEdge_Value results[1];
    WasmEdge_Value malloc_param[1];
    WasmEdge_Value params[args.size()];

    int mem_size = WasmEdge_MemoryInstanceGetPageSize(mem_ctx) * 65536;
    int mem_offset = mem_size;

    WasmEdge_Result res;
    for (unsigned int i = 0; i < args.size(); ++i) {
        int text_len = strlen(args[i]);
        const char *text = args[i];
        malloc_param[0] = WasmEdge_ValueGenI32(text_len + 2);
        WasmEdge_String wasmedge_func_name = WasmEdge_StringCreateByCString("opengauss_malloc");
        res = WasmEdge_VMExecute(vm_context, wasmedge_func_name, malloc_param, 1, results, 1);
        WasmEdge_StringDelete(wasmedge_func_name);
        if (!WasmEdge_ResultOK(res)) {
            ereport(ERROR, (errmsg("wasm_engine: call opengauss malloc failed")));
        }
        mem_offset = WasmEdge_ValueGetI32(results[0]);

        uint8_t *data = WasmEdge_MemoryInstanceGetPointer(mem_ctx, mem_offset, text_len + 2);
        data[0] = OPENGAUSS_TEXT;
        memcpy(data + 1, text, text_len);
        data[1 + text_len] = '\0';
        params[i] = WasmEdge_ValueGenI32(mem_offset);
    }

    WasmEdge_String wasmedge_func_name = WasmEdge_StringCreateByCString(funcname);
    res = WasmEdge_VMExecute(vm_context, wasmedge_func_name, params, args.size(), results, 1);
    WasmEdge_StringDelete(wasmedge_func_name);
    if (!WasmEdge_ResultOK(res)) {
        ereport(ERROR, (errmsg("wasm_engine: call func %s failed", funcname)));
    }

    int type_offset = WasmEdge_ValueGetI32(results[0]);
    char *type_ptr = (char *)WasmEdge_MemoryInstanceGetPointer(mem_ctx, type_offset, 1);
    if (!type_ptr) {
        ereport(ERROR, (errmsg("Unexpected end of Wasm memory when trying to fetch results")));
    }
    char type = *type_ptr;
    if (type != OPENGAUSS_TEXT) {
        ereport(ERROR, (errmsg("Unsupported type of wasm: %d", type)));
    }

    const char *wasm_result = type_ptr + 1;
    size_t wasm_result_len = strlen(wasm_result);
    char *result = (char *)pg_malloc(wasm_result_len + 1);
    if (!result) {
        ereport(ERROR, (errmsg("malloc memory for result failed, size: %ld", wasm_result_len)));
    }
    memcpy(result, wasm_result, wasm_result_len);
    result[wasm_result_len] = '\0';

    return result;
}
*/
static void wasm_export_funcs_query(int64 instanceid, TupleFuncState* inter_call_data)
{
    WasmVM *wasmVM = find_wasm_vm(instanceid);
    if (wasmVM == NULL) {
        ereport(ERROR, (errmsg("wasm_engine: instance with id %ld is not find", instanceid)));
    }

    std::vector<WasmFuncInfo *>* functions = find_exported_func_list(instanceid);
    if (functions != NULL) {
        inter_call_data->currindex = functions->begin();
        inter_call_data->lastindex = functions->end();
        //elog(DEBUG1, "wasm_engine:find exported func info for instanceid %ld", instanceid);
        return;
    }

    functions = new(std::nothrow)std::vector<WasmFuncInfo *>;
    exported_functions.insert(std::pair<int64, std::vector<WasmFuncInfo *>*>(instanceid, functions));
    
    WasmEdge_String func_name_list[BUF_LEN];
    const WasmEdge_FunctionTypeContext *func_type_list[BUF_LEN];
    
     * If the list length is larger than the buffer length, the overflowed data
     * will be discarded.
     */
  /*  uint32_t rel_func_num = WasmEdge_VMGetFunctionList(wasmVM->vm_context, func_name_list, func_type_list, BUF_LEN);
    for (unsigned int i = 0; i < rel_func_num && i < BUF_LEN; ++i) {
        char tmp_buffer[BUF_LEN] = {0};
        uint32_t func_name_len = WasmEdge_StringCopy(func_name_list[i], tmp_buffer, sizeof(tmp_buffer));
        // elog(DEBUG1, "wasm_engine: exported function string length: %u, name: %s\n", func_name_len, tmp_buffer);
        if (strcmp(tmp_buffer, malloc_func) == 0) {
            // elog(DEBUG1, "wasm_engine: opengauss_malloc is not need to export to user\n");
            continue;
        }

        uint32_t param_nums = WasmEdge_FunctionTypeGetParametersLength(func_type_list[i]);
        if (param_nums > MAX_PARAMS) {
            ereport(ERROR, (errmsg("wasm_engine: func %s has more than 10 params which not support", tmp_buffer)));
        }

        uint32_t return_num = WasmEdge_FunctionTypeGetReturnsLength(func_type_list[i]);
        if (return_num > MAX_RETURNS) {
            ereport(ERROR, (errmsg("wasm_engine: func %s has more than 1 return value which not support", tmp_buffer)));
        }

        WasmFuncInfo *funcinfo = new(std::nothrow)WasmFuncInfo();

        enum WasmEdge_ValType param_buffer[MAX_PARAMS]; // we allow max 10 parameters
        param_nums = WasmEdge_FunctionTypeGetParameters(func_type_list[i], param_buffer, MAX_PARAMS);
        for (unsigned int j = 0; j < param_nums; ++j) {
            if (param_buffer[j] == WasmEdge_ValType_I32) {
                funcinfo->inputs.push_back("text");
            } else if (param_buffer[j] == WasmEdge_ValType_I64) {
                funcinfo->inputs.push_back("bigint");
            } else {
                ereport(ERROR, (errmsg("wasm_engine: not support the value type(%d) for now", param_buffer[j])));
            }
        }
        
        return_num = WasmEdge_FunctionTypeGetReturns(func_type_list[i], param_buffer, 10);
        if (param_buffer[0] == WasmEdge_ValType_I32) {
            funcinfo->outputs = "text";
        } else if (param_buffer[0] == WasmEdge_ValType_I64) {
            funcinfo->outputs = "bigint";
        } else {
            ereport(ERROR, (errmsg("wasm_engine: not support the value type(%d) for now", param_buffer[0])));
        }

        funcinfo->funcname = std::string(tmp_buffer, func_name_len);
        functions->push_back(funcinfo);
    }

    inter_call_data->currindex = functions->begin();
    inter_call_data->lastindex = functions->end();
    // elog(DEBUG1, "wasm_engine:init exported func info for instanceid %ld", instanceid); 
}
*/
static WasmVM* create_wasm_instance_internal(char* filepath)
{       
    /*WasmEdge_ConfigureContext *config_context = WasmEdge_ConfigureCreate();
    WasmEdge_ConfigureAddHostRegistration(config_context, WasmEdge_HostRegistration_Wasi);
    WasmEdge_ConfigureAddHostRegistration(config_context, WasmEdge_HostRegistration_WasiNN);
    WasmEdge_VMContext *vm_cxt = WasmEdge_VMCreate(config_context, NULL);

    WasmEdge_Result result = WasmEdge_VMLoadWasmFromFile(vm_cxt, filepath);
    if (!WasmEdge_ResultOK(result)) {
        WasmEdge_ConfigureDelete(config_context);
        ereport(ERROR, (errmsg("wasm_engine: failed to load %s", filepath)));
    }

    result = WasmEdge_VMValidate(vm_cxt);
    if (!WasmEdge_ResultOK(result)) {
        WasmEdge_ConfigureDelete(config_context);
        ereport(ERROR, (errmsg("wasm_engine: wasm file validation failed %s", WasmEdge_ResultGetMessage(result))));
    }

    result = WasmEdge_VMInstantiate(vm_cxt);
    if (!WasmEdge_ResultOK(result)) {
        WasmEdge_ConfigureDelete(config_context);
        ereport(ERROR, (errmsg("wasm_engine: wasm vm initialize failed: %s", WasmEdge_ResultGetMessage(result))));
    }
    WasmEdge_ConfigureDelete(config_context);
	*/
	static char global_heap_buf[512 * 1024];
        char *buffer;
	uint8_t* buffer1 = reinterpret_cast<uint8_t*>(buffer);
	
        char error_buf[128];
	wasm_module_t module = NULL;
	wasm_module_inst_t module_inst = NULL;
	//wasm_exec_env_t exec_env = NULL;
        uint32 buf_size, stack_size = 8092, heap_size = 8092;
	RuntimeInitArgs init_args;
    
	init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
	memset(&init_args, 0, sizeof(RuntimeInitArgs));
	
	if (!wasm_runtime_full_init(&init_args)) {
        printf("Init runtime environment failed.\n");
        return 0;
    }

    buffer = bh_read_file_to_buffer(filepath, &buf_size);

    if (!buffer) {
        printf("Open wasm app file [%s] failed.\n", filepath);
        return 0;
    }

    module = wasm_runtime_load(buffer1, buf_size, error_buf, sizeof(error_buf));
    if (!module) {
        printf("Load wasm module failed. error: %s\n", error_buf);
        return 0;
    }

    module_inst = wasm_runtime_instantiate(module, stack_size, heap_size,
                                           error_buf, sizeof(error_buf),0);

    if (!module_inst) {
        printf("Instantiate wasm module failed. error: %s\n", error_buf);
        return 0;
    }
	
	
    WasmVM *wasmVM = new (std::nothrow)WasmVM();
    wasmVM->module_inst = module_inst;
    wasmVM->wasm_file = filepath;
    
    return wasmVM;
}

PG_FUNCTION_INFO_V1(wasm_create_instance);
Datum wasm_create_instance(PG_FUNCTION_ARGS) 
{
    int64 uuid = generate_uuid(PG_GETARG_DATUM(0));
    text *arg = PG_GETARG_TEXT_P(0);
    char* filepath = text_to_cstring(arg);
    canonicalize_path(filepath);

    if (!superuser())
        ereport(ERROR,
            (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE), (errmsg("wasm_engine: must be system admin to create wasm instance"))));

    WasmVM *wasmVM = find_wasm_vm(uuid);
    if (wasmVM != NULL) {
        ereport(NOTICE, (errmsg("wasm_engine: instance already created for %s", filepath)));
        return UInt32GetDatum(uuid);
    }
    
    wasmVM = create_wasm_instance_internal(filepath);
    instances.insert(std::pair<int64, WasmVM*>(uuid, wasmVM));

    return Int64GetDatum(uuid);
}

PG_FUNCTION_INFO_V1(wasm_drop_instance);
Datum wasm_drop_instance(PG_FUNCTION_ARGS) 
{
    int64 instanceid = PG_GETARG_INT64(0);
    Datum module_path;
    
    if (!superuser())
        ereport(ERROR,
            (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE), (errmsg("wasm_engine: must be system admin to delete wasm instance"))));

    std::map<int64, WasmVM*>::iterator institor = instances.begin();
    while (institor != instances.end() && institor->first != instanceid) {
        institor++;
    }
    if (institor == instances.end()) {
        ereport(ERROR, (errmsg("wasm_engine:instance with id=%ld not exist", instanceid)));
    }
    module_path = CStringGetTextDatum((institor->second->wasm_file).c_str());
    instances.erase(institor);

    std::map<int64, std::vector<WasmFuncInfo*>*>::iterator funcitor = exported_functions.begin();
    while (funcitor != exported_functions.end() && funcitor->first == instanceid) {
        exported_functions.erase(funcitor++);
    }
    
    return module_path;
}

PG_FUNCTION_INFO_V1(wasm_get_instances);
Datum wasm_get_instances(PG_FUNCTION_ARGS) 
{
    FuncCallContext* fctx = NULL;
    TupleInstanceState* inter_call_data = NULL;
    if (SRF_IS_FIRSTCALL()) {
        TupleDesc tupdesc;
        MemoryContext mctx;

        fctx = SRF_FIRSTCALL_INIT();
        mctx = MemoryContextSwitchTo(fctx->multi_call_memory_ctx);
        inter_call_data = (TupleInstanceState*)palloc(sizeof(TupleInstanceState));

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            elog(ERROR, "wasm_engine: return type must be a row type");

        inter_call_data->tupd = tupdesc;
        inter_call_data->currindex = instances.begin();
        inter_call_data->lastindex = instances.end();

        fctx->user_fctx = inter_call_data;
        MemoryContextSwitchTo(mctx);
    }

    fctx = SRF_PERCALL_SETUP();
    inter_call_data = (TupleInstanceState*)(fctx->user_fctx);
    
    if (inter_call_data->currindex != inter_call_data->lastindex) {
        HeapTuple resultTuple;
        Datum result;
        Datum values[2];
        bool nulls[2];

        //errno_t rc = memset(nulls, 0, sizeof(nulls));
        //securec_check_c(rc, "\0", "\0");

        std::string wasm_file = inter_call_data->currindex->second->wasm_file;
        values[0] = Int64GetDatum(inter_call_data->currindex->first);
        values[1] = CStringGetTextDatum(wasm_file.c_str());

        /* Build and return the result tuple. */
        resultTuple = heap_form_tuple(inter_call_data->tupd, values, nulls);
        result = HeapTupleGetDatum(resultTuple);

        inter_call_data->currindex++;
        SRF_RETURN_NEXT(fctx, result);
    } else {
        SRF_RETURN_DONE(fctx);
    }
}

/*PG_FUNCTION_INFO_V1(wasm_get_exported_functions);
Datum wasm_get_exported_functions(PG_FUNCTION_ARGS) 
{
    int64 instanceid = PG_GETARG_INT64(0);
    FuncCallContext* fctx = NULL;
    TupleFuncState* inter_call_data = NULL;
    if (SRF_IS_FIRSTCALL()) {
        TupleDesc tupdesc;
        MemoryContext mctx;

        fctx = SRF_FIRSTCALL_INIT();
        mctx = MemoryContextSwitchTo(fctx->multi_call_memory_ctx);
        inter_call_data = (TupleFuncState*)palloc(sizeof(TupleFuncState));
        
         Build a tuple descriptor for our result type 
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            elog(ERROR, "wasm_engine: return type must be a row type");

        inter_call_data->tupd = tupdesc;
        wasm_export_funcs_query(instanceid, inter_call_data);

        fctx->user_fctx = inter_call_data;
        MemoryContextSwitchTo(mctx);
    }
    fctx = SRF_PERCALL_SETUP();
    inter_call_data = (TupleFuncState*)(fctx->user_fctx);
    
    if (inter_call_data->currindex != inter_call_data->lastindex) {
        HeapTuple resultTuple;
        Datum result;
        Datum values[3];
        bool nulls[3];

        //errno_t rc = memset(nulls, 0, sizeof(nulls));
        //securec_check_c(rc, "\0", "\0");

        WasmFuncInfo *funcinfo = *inter_call_data->currindex;
        values[0] = CStringGetTextDatum(funcinfo->funcname.c_str());
        std::string inputs;
        for (std::vector<std::string>::iterator curr = funcinfo->inputs.begin(); curr != funcinfo->inputs.end(); curr++) {
            inputs += *curr;
            inputs += ",";
        }
        if (inputs.length() > 0) {
            inputs.pop_back();
        }
        values[1] = CStringGetTextDatum(inputs.c_str());
        values[2] = CStringGetTextDatum(funcinfo->outputs.c_str());

         Build and return the result tuple. 
        resultTuple = heap_form_tuple(inter_call_data->tupd, values, nulls);
        result = HeapTupleGetDatum(resultTuple);
        inter_call_data->currindex++;

        SRF_RETURN_NEXT(fctx, result);
    } else {
        SRF_RETURN_DONE(fctx);
    }
}*/

PG_FUNCTION_INFO_V1(wasm_invoke_function_int8_0);
Datum wasm_invoke_function_int8_0(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_int8_1);
Datum wasm_invoke_function_int8_1(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_int8_2);
Datum wasm_invoke_function_int8_2(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_int8_3);
Datum wasm_invoke_function_int8_3(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_int8_4);
Datum wasm_invoke_function_int8_4(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_int8_5);
Datum wasm_invoke_function_int8_5(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));
    std::vector<int64> params;

    params.push_back(PG_GETARG_INT64(2));
    params.push_back(PG_GETARG_INT64(3));
    params.push_back(PG_GETARG_INT64(4));
    params.push_back(PG_GETARG_INT64(5));
    params.push_back(PG_GETARG_INT64(6));
    int64 result = wasm_invoke_function(instanceid, funcname, params);
    return Int64GetDatum(result);
}

/*PG_FUNCTION_INFO_V1(wasm_invoke_function_text_1);
Datum wasm_invoke_function_text_1(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));

    std::vector<char*> params;
    params.push_back(TextDatumGetCString(PG_GETARG_DATUM(2)));

    char* result = wasm_invoke_function2(instanceid, funcname, params);
    return CStringGetTextDatum(result);
}

PG_FUNCTION_INFO_V1(wasm_invoke_function_text_2);
Datum wasm_invoke_function_text_2(PG_FUNCTION_ARGS)
{
    char* instanceid = TextDatumGetCString(PG_GETARG_DATUM(0));
    char* funcname = TextDatumGetCString(PG_GETARG_DATUM(1));

    std::vector<char*> params;
    params.push_back(TextDatumGetCString(PG_GETARG_DATUM(2)));
    params.push_back(TextDatumGetCString(PG_GETARG_DATUM(3)));

    char* result = wasm_invoke_function2(instanceid, funcname, params);
    return CStringGetTextDatum(result);
}*/

/*
 * Module Load Callback
 */
void _PG_init(void)
{
    /**
     * When openGauss start up and load the wasm_engine.so, it should call
     * this function to check whether it has created instances.
     * If has, call the create instance interface to restore them.
     */
    int ret;
    if ((ret = SPI_connect()) < 0) {
        elog(ERROR, "wasm_engine: SPI_connect returned %d", ret);
    }

    if ((ret = SPI_exec("SELECT id, wasm_file FROM wasm.instances", 0)) < 0) {
        elog(ERROR, "wasm_engine: SPI_connect exec query failed: %d", ret);
    }
    
    if (SPI_tuptable) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        SPITupleTable *tuptable = SPI_tuptable;

        int64 rows = SPI_processed;
        for (int i = 0; i < rows; i++) {
            HeapTuple tuple = tuptable->vals[i];
            char* instanceId = SPI_getvalue(tuple, tupdesc, 1);
            char *filepath = SPI_getvalue(tuple, tupdesc, 2);
            
            WasmVM *wasmVM = create_wasm_instance_internal(filepath);
            instances.insert(std::pair<int64, WasmVM*>(atol(instanceId), wasmVM));
        }
    }
    
    SPI_finish();
}

