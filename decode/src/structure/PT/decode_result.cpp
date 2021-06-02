#include "structure/PT/decode_result.hpp"

#include <stdlib.h>
#include <string.h>

int TraceData::expand_data(size_t size) {
    if (!data_volume) {
        data_begin = (u1 *)malloc(initial_data_volume);
        if (!data_begin)
            return -1;
        data_end = data_begin;
        data_volume = initial_data_volume;
    }
    while (data_volume - (data_end - data_begin) < size) {
        u1 *new_data = (u1 *)malloc(data_volume + initial_data_volume);
        if (!new_data)
            return -1;
        memcpy(new_data, data_begin, data_volume);
        free(data_begin);
        data_end = new_data + (data_end - data_begin);
        data_begin = new_data;
        data_volume += initial_data_volume;
    }
    return 0;
}

int TraceData::write(void *data, size_t size) {
    if (data_volume - (data_end - data_begin) < size) {
        if (expand_data(size) < 0)
            return -1;
    }
    memcpy(data_end, data, size);
    data_end += size;
    return 0;
}

TraceData::SplitKind TraceData::get_split_kind(size_t loc) {
    auto iter = split_kind_map.find(loc);
    if (iter == split_kind_map.end()) {
        return SplitKind::_NOT_SPLIT;
    }
    return iter->second;
}

bool TraceData::get_md(size_t loc, MethodDesc &md) {
    auto iter = method_desc_map.find(loc);
    if (iter != method_desc_map.end()) {
        md = iter->second;
        return true;
    }
    return false;
}

bool TraceData::get_inter(size_t loc, const u1 *&codes, size_t &size,
                          size_t &new_loc) {
    const u1 *pointer = data_begin + loc;
    if (pointer > data_end)
        return false;
    Bytecodes::Code code = Bytecodes::cast(*pointer);
    if (code != Bytecodes::_jportal_bytecode)
        return false;
    pointer++;
    const InterRecord *inter = (const InterRecord *)pointer;
    if (pointer + sizeof(InterRecord) > data_end ||
            pointer + sizeof(InterRecord) < pointer)
        return false;
    pointer += sizeof(InterRecord);
    if (pointer + inter->size > data_end || pointer + inter->size < pointer)
        return false;
    codes = pointer;
    size = inter->size;
    new_loc = pointer - data_begin + inter->size;
    return true;
}

bool TraceData::get_jit(size_t loc, const PCStackInfo **&codes, size_t &size,
                        const jit_section *&section, size_t &new_loc) {
    const u1 *pointer = data_begin + loc;
    if (pointer > data_end)
        return false;
    Bytecodes::Code code = Bytecodes::cast(*pointer);
    if (code != Bytecodes::_jportal_jitcode_entry &&
            code != Bytecodes::_jportal_jitcode)
        return false;
    pointer++;
    const JitRecord *jit = (const JitRecord *)pointer;
    if (pointer + sizeof(JitRecord) > data_end ||
            pointer + sizeof(JitRecord) < pointer)
        return false;
    pointer += sizeof(JitRecord);
    if (pointer + jit->size * sizeof(PCStackInfo *) > data_end ||
            pointer + jit->size * sizeof(PCStackInfo *) < pointer)
        return false;
    codes = (const PCStackInfo **)pointer;
    size = jit->size;
    section = jit->section;
    new_loc = pointer - data_begin + jit->size * sizeof(PCStackInfo *);
    return true;
}

bool TraceData::get_inter(size_t loc, vector<size_t> &loc_list) {
    if (split_kind_map.find(loc) == split_kind_map.end())
        return false;
    loc_list.push_back(loc);
    auto iter = split_map.find(loc);
    if (iter == split_map.end())
        return true;
    for (auto l : iter->second) {
        loc_list.push_back(l);
    }
    return true;
}

int TraceDataRecord::add_bytecode(u8 time, Bytecodes::Code bytecode) {
    current_time = time;
    if (code_type != Bytecodes::_jportal_bytecode) {
        size_t begin = trace.data_end - trace.data_begin;
        if (dump_cnt > 0) {
            trace.split_kind_map[begin] = TraceData::_MAY_LOSS;
            dump_cnt--;
        } else if (code_type == Bytecodes::_jportal_method_entry) {
            trace.split_kind_map[begin] = TraceData::_TAIL_LOSS;
            call_stack.push(begin);
        } else if (call_stack.empty()) {
            trace.split_kind_map[begin] = TraceData::_HEAD_TAIL_LOSS;
            call_stack.push(begin);
        }
        if (!call_stack.empty() && call_stack.top() != begin) {
            trace.split_map[call_stack.top()].push_back(begin);
            dump_list.push_front(make_pair(call_stack.top(), begin));
            if (dump_list.size() > dump_number)
                dump_list.pop_back();
        } else {
            dump_list.push_front(make_pair(begin, begin));
            if (dump_list.size() > dump_number)
                dump_list.pop_back();
        }
        Bytecodes::Code prev_code = code_type;
        code_type = Bytecodes::_jportal_bytecode;
        if (trace.write(&code_type, 1) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        InterRecord inter;
        if (trace.write(&inter, sizeof(inter)) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        loc = trace.data_end - trace.data_begin - sizeof(u8);
        if (prev_code == Bytecodes::_jportal_exception_handling) {
            if (trace.write(&prev_code, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            (*((u8 *)(trace.data_begin + loc)))++;
        }
    }
    if (trace.write(&bytecode, 1) < 0) {
        fprintf(stderr, "trace data record: fail to write.\n");
        return -1;
    }
    (*((u8 *)(trace.data_begin + loc)))++;
    if (Bytecodes::is_return(bytecode)) {
        if (!call_stack.empty()) {
            TraceData::SplitKind kind = trace.split_kind_map[call_stack.top()];
            if (kind == TraceData::_HEAD_TAIL_LOSS)
                trace.split_kind_map[call_stack.top()] = TraceData::_HEAD_LOSS;
            else if (kind == TraceData::_TAIL_LOSS)
                trace.split_kind_map[call_stack.top()] = TraceData::_NO_LOSS;
            call_stack.pop();
        }
        code_type = Bytecodes::_illegal;
    }
    bytecode_type = bytecode;
    return 0;
}

int TraceDataRecord::add_branch(u1 taken) {
    if (!Bytecodes::is_branch(bytecode_type) ||
            code_type != Bytecodes::_jportal_bytecode) {
        fprintf(stderr, "trace data record: non branch bytecode.\n");
        return -1;
    }
    if (trace.write(&taken, 1) < 0) {
        fprintf(stderr, "trace data record: fail to write.\n");
        return -1;
    }
    (*((u8 *)(trace.data_begin + loc)))++;
    return 0;
}

int TraceDataRecord::add_jitcode(u8 time, const jit_section *section,
                                 PCStackInfo *pc, bool entry) {
    current_time = time;
    if (code_type != Bytecodes::_jportal_jitcode
         && code_type != Bytecodes::_jportal_jitcode_entry
         || last_section != section) {
        code_type = entry ? Bytecodes::_jportal_jitcode_entry :
                            Bytecodes::_jportal_jitcode;
        if (trace.write(&code_type, 1) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        JitRecord jit(section);
        if (trace.write(&jit, sizeof(jit)) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        loc = trace.data_end - trace.data_begin - 2 * sizeof(u8);
        last_section = section;
    }
    if (trace.write(&pc, sizeof(pc)) < 0) {
        fprintf(stderr, "trace data record: fail to write.\n");
        return -1;
    }
    (*((u8 *)(trace.data_begin + loc)))++;
    bytecode_type = Bytecodes::_illegal;
    return 0;
}

int TraceDataRecord::add_codelet(CodeletsEntry::Codelet codelet) {
    bytecode_type = Bytecodes::_illegal;
    switch (codelet) {
        case CodeletsEntry::_method_entry_point: {
            code_type = Bytecodes::_jportal_method_entry;
            if (trace.write(&code_type, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            return 0;
        }
        case CodeletsEntry::_throw_exception_entrypoints:
        case CodeletsEntry::_rethrow_exception: {
            if (code_type == Bytecodes::_jportal_exception_handling) {
                if (!call_stack.empty())
                    call_stack.pop();
            }
            code_type = Bytecodes::_jportal_throw_exception;
            if (trace.write(&code_type, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            return 0;
        }
        case CodeletsEntry::_invoke_return_entry_points: {
            if (code_type == Bytecodes::_jportal_method_entry) {
                trace.data_end--;
                code_type = Bytecodes::_illegal;
                return 0;
            }
            code_type = Bytecodes::_jportal_invoke_return_entry_points;
            if (trace.write(&code_type, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            return 0;
        }
        case CodeletsEntry::_deoptimization_entry_points: {
            code_type = Bytecodes::_jportal_deoptimization_entry_points;
            if (trace.write(&code_type, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            call_stack = stack<size_t>();
            return 0;
        }
        case CodeletsEntry::_exception_handling: {
            code_type = Bytecodes::_jportal_exception_handling;
            if (trace.write(&code_type, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            return 0;
        }
        case CodeletsEntry::_result_handlers_for_native_calls: {
            if (code_type == Bytecodes::_jportal_method_entry)
                trace.data_end--;
            code_type = Bytecodes::_illegal;
            return 0;
        }
        default: {
            code_type = Bytecodes::_illegal;
            call_stack = stack<size_t>();
            return 0;
        }
    }
}

void TraceDataRecord::add_method_desc(MethodDesc md) {
    if (code_type == Bytecodes::_jportal_method_entry)
        trace.method_desc_map[trace.data_end - trace.data_begin] = md;
    return;
}

int TraceDataRecord::add_osr_entry() {
    if (bytecode_type != Bytecodes::_goto && bytecode_type != 
            Bytecodes::_goto_w && !Bytecodes::is_branch(bytecode_type))
        return 0;
    code_type = Bytecodes::_jportal_osr_entry_points;
    bytecode_type = Bytecodes::_illegal;
    if (trace.write(&code_type, 1) < 0) {
        fprintf(stderr, "trace data record: fail to write.\n");
        return -1;
    }
    if (!call_stack.empty())
        call_stack.pop();
    return 0;
}

void TraceDataRecord::switch_out(bool loss) {
    if (!dump_list.empty())
        dump_cnt = dump_number;
    for (auto offset : dump_list) {
        auto iter = trace.split_map.find(offset.first);
        if (iter == trace.split_map.end())
            continue;
        if (iter->second.empty())
            continue;
        iter->second.pop_back();
        trace.split_kind_map[offset.second] = TraceData::_MAY_LOSS;
    }
    dump_list = list<pair<size_t, size_t>>();
    call_stack = stack<size_t>();
    code_type = Bytecodes::_illegal;
    bytecode_type = Bytecodes::_illegal;
    if (thread) {
        thread->end_addr = trace.data_end - trace.data_begin;
        thread->end_time = current_time;
        thread->tail_loss = loss;
    }
    thread = nullptr;
    return;
}

void TraceDataRecord::switch_in(long tid, u8 time, bool loss) {
    if (thread && thread->tid == tid && !loss)
        return;

    current_time = time;
    auto split = trace.thread_map.find(tid);
    if (split == trace.thread_map.end()) {
        trace.thread_map[tid].push_back(
            ThreadSplit(tid, trace.data_end - trace.data_begin, time));
        thread = &trace.thread_map[tid].back();
        thread->head_loss = loss;
        code_type = Bytecodes::_illegal;
        call_stack = stack<size_t>();
        return;
    }
    auto iter = split->second.begin();
    for (; iter != split->second.end(); iter++) {
        if (time < iter->start_time)
            break;
    }
    iter = split->second.insert(
            iter, ThreadSplit(tid, trace.data_end - trace.data_begin, time));
    thread = &*iter;
    thread->head_loss = loss;
    call_stack = stack<size_t>();
    code_type = Bytecodes::_illegal;
    bytecode_type = Bytecodes::_illegal;
    return;
}

bool TraceDataAccess::next_trace(Bytecodes::Code &code, size_t &loc) {
    loc = current - trace.data_begin;
    if (current >= terminal) {
        return false;
    }
    code = Bytecodes::cast(*current);
    if (code < Bytecodes::_jportal_bytecode ||
            code > Bytecodes::_jportal_osr_entry_points) {
        fprintf(stderr, "trace data access: format error %ld.\n", loc);
        current = terminal;
        loc = current - trace.data_begin;
        return false;
    }
    current++;
    if (code == Bytecodes::_jportal_bytecode) {
        const InterRecord *inter = (const InterRecord *)current;
        if (current + sizeof(InterRecord) + inter->size > trace.data_end ||
            current + sizeof(InterRecord) + inter->size < current) {
            fprintf(stderr, "trace data access: format error %ld.\n", loc);
            current = terminal;
            loc = current - trace.data_begin;
            return false;
        }
        current = current + sizeof(InterRecord) + inter->size;
    } else if (code == Bytecodes::_jportal_jitcode_entry ||
                    code == Bytecodes::_jportal_jitcode) {
        const JitRecord *jit = (const JitRecord *)current;
        if (current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *) > trace.data_end ||
                current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *) < current) {
            fprintf(stderr, "trace data access: format error %ld.\n", loc);
            current = terminal;
            loc = current - trace.data_begin;
            return false;
        }
        current = current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *);
    }
    return true;
}
