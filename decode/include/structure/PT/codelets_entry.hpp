#ifndef CODELETS_ENTRY_HPP
#define CODELETS_ENTRY_HPP

#include "structure/java/bytecodes.hpp"

#include <stdint.h>

#define address uint64_t

class CodeletsEntry {
    public:
        const static int number_of_states = 10;
        const static int number_of_return_entries = 6;
        const static int number_of_return_addrs = 10;
        const static int number_of_method_entries = 34;
        const static int number_of_result_handlers = 10;
        const static int number_of_deopt_entries = 7;
        const static int number_of_codes = 239;
        const static int number_of_codelets = 272;

        enum Codelet {
            _illegal = -1,
            
            _slow_signature_handler,
            _error_exits,
            _bytecode_tracing_support,
            _return_entry_points,
            _invoke_return_entry_points,
            _earlyret_entry_points,
            _result_handlers_for_native_calls,
            _safepoint_entry_points,
            _exception_handling,
            _throw_exception_entrypoints,
            _method_entry_point,

            _start_of_bytecodes = 32,
            _bytecode = _start_of_bytecodes,
            _return_bytecode,
            _throw_bytecode,

            _rethrow_exception,
            
            _throw_ArrayIndexOutOfBoundsException,
            _throw_ArrayStoreException,
            _throw_ArithmeticException,
            _throw_ClassCastException,
            _throw_NullPointerException,
            _throw_StackOverflowError,

            _should_not_reach_here = 270,
            _deoptimization_entry_points,            
        };
    private:
        static bool TracingBytecodes;
        static address low_bound;
        static address high_bound;
        /* start and end addresses of jvm generated codelets */
        static address _start[number_of_codelets];
        static address _end[number_of_codelets];

        /* dispatch table for each bytecode */
        static address _dispatch_table[number_of_states][number_of_codes];
        static address _trace_code[number_of_states];
        static address _return_entry[number_of_states][number_of_return_entries];
        static address _invoke_return_entry[number_of_return_addrs];
        static address _invokeinterface_return_entry[number_of_return_addrs];
        static address _invokedynamic_return_entry[number_of_return_addrs];
        static address _earlyret_entry[number_of_states];
        static address _safept_entry[number_of_states];
        static address _entry_table[number_of_method_entries];
        static address _native_abi_to_tosca[number_of_result_handlers];
        static address _deopt_entry[number_of_states][number_of_deopt_entries];

        static address _slow_signature_handler_entry;

        static address _rethrow_exception_entry;
        static address _throw_exception_entry;
        static address _remove_activation_preserving_args_entry;
        static address _remove_activation_entry;

        static address _throw_ArrayIndexOutOfBoundsException_entry;
        static address _throw_ArrayStoreException_entry;
        static address _throw_ArithmeticException_entry;
        static address _throw_ClassCastException_entry;
        static address _throw_NullPointerException_entry;
        static address _throw_StackOverflowError_entry;
        
    public:
        /* intialize entries of all codelets */
        static int entry_init(bool TracingBytecodes, const uint64_t codelets_address[]);
        
        /* match an instruction pointer address to a codelet
         * return the type(in enum Type{})
         * and if the type is bytecode(_branch_bytecode, ...)
         * argument bytecode saves the bytecode */
        static Codelet entry_match(address ip, Bytecodes::Code &code);
};

#endif