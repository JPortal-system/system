#include "decoder/jvm_dump_decoder.hpp"
#include "structure/PT/load_file.hpp"
#include "structure/PT/jit_section.hpp"
#include "structure/PT/codelets_entry.hpp"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t *JvmDumpDecoder::begin = nullptr;
uint8_t *JvmDumpDecoder::end = nullptr;
map<long, long> JvmDumpDecoder::thread_map;
map<int, MethodDesc *> JvmDumpDecoder::md_map;
map<const uint8_t *, jit_section *> JvmDumpDecoder::section_map;
map<const uint8_t *, CompiledMethodDesc *> JvmDumpDecoder::cmd_map;

JvmDumpDecoder::DumpInfoType JvmDumpDecoder::dumper_event(uint64_t time, long tid,
                                                const void *&data) {
    const DumpInfo *info;
    data = nullptr;
    if (current < end) {
        info = (const struct DumpInfo *)current;
        if (current + info->size > end || info->time > time)
           return _illegal;
        current += sizeof(DumpInfo);
        switch(info->type) {
            case _interpreter_info: {
                data = current;
                current += sizeof(InterpreterInfo);
                return _interpreter_info;
            }
            case _method_entry_initial: {
                const MethodEntryInitial* me;
                me = (const MethodEntryInitial*)current;
                current += sizeof(MethodEntryInitial);
                const char *klass_name = (const char *)current;
                current += me->klass_name_length;
                const char *name = (const char *)current;
                current += me->method_name_length;
                const char *signature = (const char *)current;
                current += me->method_signature_length;
                auto iter1 = thread_map.find(tid);
                if (iter1 == thread_map.end() || iter1->second != me->tid)
                    return _no_thing;
                auto iter = md_map.find(me->idx);
                if (iter == md_map.end())
                    return _no_thing;
                data = iter->second;
                return _method_entry;
            }
            case _method_entry: {
                const MethodEntryInfo* me;
                me = (const MethodEntryInfo*)current;
                current += sizeof(MethodEntryInfo);
                auto iter1 = thread_map.find(tid);
                if (iter1 == thread_map.end() || iter1->second != me->tid)
                    return _no_thing;
                auto iter = md_map.find(me->idx);
                if (iter == md_map.end())
                    return _no_thing;
                data = iter->second;
                return _method_entry;
            }
            case _method_exit: {
                const MethodExitInfo* me;
                me = (const MethodExitInfo*)current;
                current += sizeof(MethodExitInfo);
                auto iter1 = thread_map.find(tid);
                if (iter1 == thread_map.end() || iter1->second != me->tid)
                    return _no_thing;
                auto iter = md_map.find(me->idx);
                if (iter == md_map.end())
                    return _no_thing;
                data = iter->second;
                return _method_exit;
            }
            case _compiled_method_load: {
                current += (info->size - sizeof(info));
                auto iter = section_map.find(current);
                if (iter == section_map.end())
                    return _no_thing;
                data = iter->second;
                return _compiled_method_load;
            }
            case _compiled_method_unload: {
                data = current;
                current += sizeof(CompiledMethodUnloadInfo);
                return _compiled_method_unload;
            }
            case _dynamic_code_generated: {
                current += (info->size - sizeof(info));
                auto iter = section_map.find(current);
                if (iter == section_map.end())
                    return _no_thing;
                data = iter->second;
                return _dynamic_code_generated;
            }
            case _thread_start: {
                data = current;
                const ThreadStartInfo *th;
                th = (const ThreadStartInfo*)current;
                current += sizeof(ThreadStartInfo);
                return _thread_start;
            }
            case _inline_cache_add: {
                data = current;
                current += sizeof(InlineCacheAdd);
                return _inline_cache_add;
            }
            case _inline_cache_clear: {
                data = current;
                current += sizeof(InlineCacheClear);
                return _inline_cache_clear;
            }
            default: {
                current = end;
                return _illegal;
            }
        }
    }
    return _illegal;
}

long JvmDumpDecoder::get_java_tid(long tid) {
    auto iter = thread_map.find(tid);
    if (iter == thread_map.end())
        return -1;
    return iter->second;
}

void JvmDumpDecoder::initialize(char *dump_data) {
    uint8_t *buffer;
    size_t size;
    uint64_t foffset, fsize;
    int errcode;

    errcode = preprocess_filename(dump_data, &foffset, &fsize);
    if (errcode < 0) {
        fprintf(stderr, "JvmDumpDecoder: bad file: %s.\n", dump_data);
        return;
    }
    errcode = load_file(&buffer, &size, dump_data,
                                foffset, fsize, "main");
    if (errcode < 0) {
        fprintf(stderr, "JvmDumpDecoder: bad file: %s.\n", dump_data);
        return;
    }

    begin = buffer;
    end = buffer + size;
    const DumpInfo *info;
    while (buffer < end) {
        info = (const struct DumpInfo *)buffer;
        if (buffer + info->size > end)
           return;
        buffer += sizeof(DumpInfo);
        switch(info->type) {
            case _interpreter_info: {
                auto inter = (JvmDumpDecoder::InterpreterInfo *)buffer;
                buffer += sizeof(InterpreterInfo);
                CodeletsEntry::entry_init(inter->TraceBytecodes, inter->codelets_address);
                break;
            }
            case _method_entry_initial: {
                const MethodEntryInitial* me;
                me = (const MethodEntryInitial*)buffer;
                buffer += sizeof(MethodEntryInitial);
                const char *klass_name = (const char *)buffer;
                buffer += me->klass_name_length;
                const char *name = (const char *)buffer;
                buffer += me->method_name_length;
                const char *signature = (const char *)buffer;
                buffer += me->method_signature_length;
                MethodDesc *md = new MethodDesc(me->klass_name_length,
                        me->method_name_length, me->method_signature_length,
                        klass_name, name, signature);
                md_map[me->idx] = md;
                break;
            }
            case _method_entry: {
                const MethodEntryInfo* me;
                me = (const MethodEntryInfo*)buffer;
                buffer += sizeof(MethodEntryInfo);
                break;
            }
            case _method_exit: {
                const MethodExitInfo* me;
                me = (const MethodExitInfo*)buffer;
                buffer += sizeof(MethodExitInfo);
                break;
            }
            case _compiled_method_load: {
                const CompiledMethodLoadInfo *cm;
                cm = (const CompiledMethodLoadInfo*)buffer;
                buffer += sizeof(CompiledMethodLoadInfo);
                map<int, MethodDesc> method_desc;
                MethodDesc main_md;
                for (int i = 0; i < cm->inline_method_cnt; i++) {
                    const InlineMethodInfo*imi;
                    imi = (const InlineMethodInfo*)buffer;
                    buffer += sizeof(InlineMethodInfo);
                    const char *klass_name = (const char *)buffer;
                    buffer += imi->klass_name_length;
                    const char *name = (const char *)buffer;
                    buffer += imi->name_length;
                    const char *signature = (const char *)buffer;
                    buffer += imi->signature_length;
                    MethodDesc md(
                            imi->klass_name_length, imi->name_length,
                            imi->signature_length, klass_name, 
                            name, signature);
                    if (i == 0)
                        main_md = md;
                    method_desc.insert(make_pair(imi->method_index, md));
                }
                const uint8_t *insts, *scopes_pc, *scopes_data;
                insts = (const uint8_t *)buffer;
                buffer += cm->code_size;
                scopes_pc = (const uint8_t *)buffer;
                buffer += cm->scopes_pc_size;
                scopes_data = (const uint8_t *)buffer;
                buffer += cm->scopes_data_size;                
                CompiledMethodDesc *cmd = new CompiledMethodDesc(cm->scopes_pc_size,
                      cm->scopes_data_size, cm->entry_point, cm->verified_entry_point,
                      cm->osr_entry_point, cm->inline_method_cnt, main_md, method_desc);
                jit_section *section;
                int errcode = jit_mk_section(&section, insts, cm->code_begin, cm->code_size,
                                                scopes_pc, scopes_data, cmd, nullptr);
                if (errcode < 0) {
                    fprintf(stderr, "JvmDumpDecoder: fail to make section.\n");
                    delete cmd;
                    break;
                }
                errcode = jit_section_get(section);
                if (errcode < 0) {
                    fprintf(stderr, "JvmDumpDecoder: fail to get section.\n");
                    delete cmd;
                    jit_section_free(section);
                    break;
                }
                section_map[buffer] = section;
                cmd_map[buffer] = cmd;
                break;
            }
            case _compiled_method_unload: {
                buffer += sizeof(CompiledMethodUnloadInfo);
                break;
            }
            case _dynamic_code_generated: {
                const DynamicCodeGenerated *dcg;
                dcg = (const DynamicCodeGenerated *)buffer;
                buffer += sizeof(DynamicCodeGenerated);
                const char *name;
                const uint8_t *code;
                name = (const char *)buffer;
                buffer += dcg->name_length;
                code = buffer;
                buffer += dcg->code_size;
                jit_section *section;
                int errcode = jit_mk_section(&section, code, dcg->code_begin, dcg->code_size,
                                                nullptr, nullptr, nullptr, name);
                if (errcode < 0) {
                    fprintf(stderr, "JvmDumpDecoder: fail to make section.\n");
                    break;
                }
                errcode = jit_section_get(section);
                if (errcode < 0) {
                    fprintf(stderr, "JvmDumpDecoder: fail to get section.\n");
                    jit_section_free(section);
                    break;
                }
                section_map[buffer] = section;
                break;
            }
            case _thread_start: {
                const ThreadStartInfo *th;
                th = (const ThreadStartInfo*)buffer;
                buffer += sizeof(ThreadStartInfo);
                thread_map[th->sys_tid] = th->java_tid;
                break;
            }
            case _inline_cache_add: {
                const InlineCacheAdd *ic;
                ic = (const InlineCacheAdd*)buffer;
                buffer += sizeof(InlineCacheAdd);
                break;
            }
            case _inline_cache_clear: {
                const InlineCacheClear *ic;
                ic = (const InlineCacheClear*)buffer;
                buffer += sizeof(InlineCacheClear);
                break;
            }
            default: {
                buffer = end;
                fprintf(stderr, "JvmDumpDecoder: unknown dump type.\n");
                return;
            }
        }
    }
    return;
}

void JvmDumpDecoder::destroy() {
    for (auto section : section_map)
        jit_section_put(section.second);
    section_map.clear();

    for (auto md : md_map)
        delete md.second;
    md_map.clear();

    for (auto cmd : cmd_map)
        delete cmd.second;
    cmd_map.clear();

    free(begin);
    begin = nullptr;
    end = nullptr;
}