#include "structure/PT/codelets_entry.hpp"

#include <cstdio>

using namespace std;

bool CodeletsEntry::TracingBytecodes;
address CodeletsEntry::low_bound;
address CodeletsEntry::high_bound;
/* start and end addresses of bytecode codelets */
address CodeletsEntry::_start[number_of_codelets];
address CodeletsEntry::_end[number_of_codelets];
/* dispatch table for each bytecode */
address CodeletsEntry::_dispatch_table[number_of_states][number_of_codes];
address CodeletsEntry::_trace_code[number_of_states];
address CodeletsEntry::_return_entry[number_of_states][number_of_return_entries];
address CodeletsEntry::_invoke_return_entry[number_of_return_addrs];
address CodeletsEntry::_invokeinterface_return_entry[number_of_return_addrs];
address CodeletsEntry::_invokedynamic_return_entry[number_of_return_addrs];
address CodeletsEntry::_earlyret_entry[number_of_states];
address CodeletsEntry::_safept_entry[number_of_states];
address CodeletsEntry::_entry_table[number_of_method_entries];
address CodeletsEntry::_native_abi_to_tosca[number_of_result_handlers];
address CodeletsEntry::_deopt_entry[number_of_states][number_of_deopt_entries];

address CodeletsEntry::_slow_signature_handler_entry;
address CodeletsEntry::_rethrow_exception_entry;
address CodeletsEntry::_throw_exception_entry;
address CodeletsEntry::_remove_activation_preserving_args_entry;
address CodeletsEntry::_remove_activation_entry;

address CodeletsEntry::_throw_ArrayIndexOutOfBoundsException_entry;
address CodeletsEntry::_throw_ArrayStoreException_entry;
address CodeletsEntry::_throw_ArithmeticException_entry;
address CodeletsEntry::_throw_ClassCastException_entry;
address CodeletsEntry::_throw_NullPointerException_entry;
address CodeletsEntry::_throw_StackOverflowError_entry;

int CodeletsEntry::entry_init(bool _TracingBytecodes, const uint64_t codelets_address[]){
    TracingBytecodes = _TracingBytecodes;
    int i, j;
    int codelet_pointer = 0;
    low_bound = codelets_address[codelet_pointer++];
    high_bound = codelets_address[codelet_pointer++];
    for (i = 0; i < 2; i++) {
        _start[i] = codelets_address[codelet_pointer++];
        _end[i] = codelets_address[codelet_pointer++];
    }
    if (TracingBytecodes){
        _start[i] = codelets_address[codelet_pointer++];
        _end[i] = codelets_address[codelet_pointer++];
    } else {
        _start[2] = _end[1];
        _end[2] = _end[1];
    }

    for (i = 3; i < number_of_codelets; i++) {
        _start[i] = codelets_address[codelet_pointer++];
        _end[i] = codelets_address[codelet_pointer++];
    }
    
    for (i = 0; i < number_of_codes; i++)
        for (j = 0; j < number_of_states; j++)
            _dispatch_table[j][i] = codelets_address[codelet_pointer++];

    _slow_signature_handler_entry = codelets_address[codelet_pointer++];
    if (TracingBytecodes) {
        for (j = 0; j < number_of_states; j++)
            _trace_code[j] = codelets_address[codelet_pointer++];
    }

    for (i = 0; i < number_of_return_entries; i++)
        for (j = 0; j < number_of_states; j++)
            _return_entry[j][i] = codelets_address[codelet_pointer++];

    for (i = 0; i < number_of_return_addrs; i++){
        _invoke_return_entry[i] = codelets_address[codelet_pointer++];
        _invokeinterface_return_entry[i] = codelets_address[codelet_pointer++];
        _invokedynamic_return_entry[i] = codelets_address[codelet_pointer++];
    }
    
    for (i = 0; i < number_of_states; i++)
        _earlyret_entry[i] = codelets_address[codelet_pointer++];
    
    for (i = 0; i < number_of_result_handlers; i++)
        _native_abi_to_tosca[i] = codelets_address[codelet_pointer++];
    for (i = 0; i < number_of_states; i++)
        _safept_entry[i] = codelets_address[codelet_pointer++];
    
    _rethrow_exception_entry = codelets_address[codelet_pointer++];
    _throw_exception_entry = codelets_address[codelet_pointer++];
    _remove_activation_preserving_args_entry = codelets_address[codelet_pointer++];
    _remove_activation_entry = codelets_address[codelet_pointer++];
    _throw_ArrayIndexOutOfBoundsException_entry = codelets_address[codelet_pointer++];
    _throw_ArrayStoreException_entry = codelets_address[codelet_pointer++];
    _throw_ArithmeticException_entry = codelets_address[codelet_pointer++];
    _throw_ClassCastException_entry = codelets_address[codelet_pointer++];
    _throw_NullPointerException_entry = codelets_address[codelet_pointer++];
    _throw_StackOverflowError_entry = codelets_address[codelet_pointer++];
    for (i = 0; i < number_of_method_entries; i++)
        _entry_table[i] = codelets_address[codelet_pointer++];
    
    for (i = 0; i < number_of_deopt_entries; i++)
        for (j = 0; j < number_of_states; j++)
            _deopt_entry[j][i] = codelets_address[codelet_pointer++];
    return 0;
}

CodeletsEntry::Codelet CodeletsEntry::entry_match(address ip, Bytecodes::Code &code) {
    if ( ip < _start[0] || ip >= _end[number_of_codelets-1] )
        return _illegal;
    
    /* check to which bytecode this ip belongs */
    int ind = -1;
    int low = 0;
    int high = number_of_codelets-1;
    int middle;
    while( low <= high ){
        middle = ( low + high )/2;
        if ( ip >= _start[middle] && ip < _end[middle]){
            ind = middle;
            break;
        }
        else if ( ip >= _end[middle] && ip < _start[middle+1])
            break;
        else if ( ip < _start[middle] )
            high = middle-1;
        else
            low = middle+1;
        
    }
    if (ind < 0)
        return _illegal;
    else if (ind == _slow_signature_handler) {
        //printf("slow signature handler %llx\n", ip);
        if (ip == _slow_signature_handler_entry)
            return _slow_signature_handler;
        return _illegal;
    } else if (ind == _error_exits) {
        //printf("error exits %llx\n", ip);
        return _error_exits;
    } else if (ind == _bytecode_tracing_support) {
        for (int i = 0; i < number_of_states; i++) {
            if (_trace_code[i] == ip)
                return _bytecode_tracing_support;
        }
        return _illegal;
    } else if (ind == _return_entry_points) {
        //printf("return entry points %llx\n", ip);
        for (int i = 0; i < number_of_return_entries; i++) {
            for (int j = 0; j < number_of_states; j++) {
                if (_return_entry[j][i] == ip)
                    return _return_entry_points;
            }
        }
        return _illegal;
    } else if (ind == _invoke_return_entry_points) {
        for (int i = 0; i < number_of_return_addrs; i++) {
            if (_invoke_return_entry[i] == ip ||
                _invokeinterface_return_entry[i] == ip ||
                _invokedynamic_return_entry[i] == ip) {
                //printf("invoke return entry %llx\n", ip);
                return _invoke_return_entry_points;
            }
        }
        return _illegal;
    } else if (ind == _earlyret_entry_points) {
        //printf("earlyret entry points %llx\n", ip);
        for (int j = 0; j < number_of_states; j++) {
            if (_earlyret_entry[j] == ip)
                return _earlyret_entry_points;
        }
        return _illegal;
    } else if (ind == _result_handlers_for_native_calls) {
        //printf("result handlers for native calls %llx\n", ip);
        for (int i = 0; i < number_of_result_handlers; i++) {
            if (_native_abi_to_tosca[i] == ip)
                return _result_handlers_for_native_calls;
        }
        return _illegal;
    } else if (ind == _safepoint_entry_points) {
        //printf("safepoint entry %llx\n", ip);
        for (int j = 0; j < number_of_states; j++) {
            if (_safept_entry[j] == ip)
                return _safepoint_entry_points;
        }
        return _illegal;
    } else if (ind == _exception_handling) {
        if (ip == _rethrow_exception_entry) {
            //printf("rethrow exception %llx\n", ip);
            return _rethrow_exception;
        } else {
            //printf("exception handling %llx\n", ip);
            return _exception_handling;
        }
    } else if (ind == _throw_exception_entrypoints) {
        //printf("throw exception %llx\n", ip);
        return _throw_exception_entrypoints;
    } else if (ind < _start_of_bytecodes) {
        for (int i = 0; i < number_of_method_entries; i++) {
            if (_entry_table[i] == ip){
                //printf("method entry %llx\n", ip);
                return _method_entry_point;
            } 
        }
        return _illegal;
    } else if (ind < _start_of_bytecodes + number_of_codes)
        ind -= _start_of_bytecodes;
    else if (ind == _deoptimization_entry_points) {
        for (int i = 0; i < number_of_deopt_entries; i++){
            for (int j = 0; j < number_of_states; j++) {
                if (_deopt_entry[j][i] == ip){
                    //printf("deoptimization entry points %llx\n", ip);
                    return _deoptimization_entry_points;
                }
            }
        }
        return _illegal;
    }
    else
        return _illegal;
    
    /* bytecode */
    /* check if in dispatch table */
    for (int i = 0; i < number_of_states; i++){
        if (_dispatch_table[i][ind] == ip){
            code = Bytecodes::cast(ind);
            //printf("Bytecode %s %llx\n", Bytecodes::name_for(code), ip);
            return _bytecode;
        }
    }

    /* not in dispatch table */
    return _illegal;
}
