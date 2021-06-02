#include <thread>
#include <iostream>
#include <cstring>
#include <algorithm>

#include "structure/task/task.hpp"
#include "structure/task/worker.hpp"
#include "structure/task/task_manager.hpp"
#include "structure/java/bytecodes.hpp"
#include "structure/PT/load_file.hpp"
#include "decoder/ptjvm_decoder.hpp"
#include "decoder/jvm_dump_decoder.hpp"
#include "matcher/method_matcher.hpp"

class DecodeTask: public Task {
private:
    const char *trace_data;
    TracePart tracepart;
    TraceData &trace;
    Analyser *analyser;

public:
    DecodeTask(TracePart _tracepart, TraceData &_trace,
                Analyser *_analyser) :
                    Task(TaskKind::DECODETASK),
                    tracepart(_tracepart),
                    trace(_trace),
                    analyser(_analyser) {}
protected:
    Task* doTask() {
        ptjvm_decode(tracepart, TraceDataRecord(trace), analyser);
        free(tracepart.pt_buffer);
        free(tracepart.sb_buffer);
        return nullptr;
    };
};

class MatchTask: public Task {
private:
    MethodMatcher *matcher;
    long tid;
    ThreadSplit split;
    static mutex mu;
public:
    static map<long, vector<pair<ThreadSplit, MethodMatcher*>>> threads;

    MatchTask(MethodMatcher *_matcher, long _tid, ThreadSplit _split):
                Task(TaskKind::MATCHTASK),
                matcher(_matcher),
                tid(_tid),
                split(_split) {}
protected:
    Task* doTask() {
        if (matcher)
            matcher->match();

        std::unique_lock<std::mutex> guard(MatchTask::mu);
        threads[tid].push_back(make_pair(split, matcher));
        return nullptr;
    };
};

mutex MatchTask::mu;
map<long, vector<pair<ThreadSplit, MethodMatcher*>>> MatchTask::threads;

void decode_output(map<int, list<TracePart>> &traceparts, list<TraceData*> &traces,
                    Analyser *analyser) {
    const unordered_map<const Method *, int> &method_map = 
                                analyser->get_method_map();
    auto iter1 = traceparts.begin();
    auto iter2 = traces.begin();
    while (iter1 != traceparts.end()) {
        char name[32];
        sprintf(name, "cpu%d", iter1->first);
        FILE *fp = fopen(name, "w");
        auto iter3 = iter1->second.begin();
        size_t pt_offset = 0;
        while (iter3 != iter1->second.end()) {
            if (iter2 == traces.end() || !(*iter2))
                return;
            TraceData *trace = *iter2;
            fprintf(fp, "#%ld %ld\n", pt_offset, pt_offset + iter3->pt_size);
            pt_offset += iter3->pt_size;
            unordered_map<long, list<ThreadSplit>> &thread_map = trace->get_thread_map();
            for (auto iter4 : thread_map) {
                for (auto iter5 : iter4.second) {
                    fprintf(fp, "*%lld %lld %ld\n", iter5.start_time, iter5.end_time, iter5.tid);
                    TraceDataAccess access(*trace, iter5.start_addr, iter5.end_addr);
                    Bytecodes::Code code;
                    size_t loc;
                    while (access.next_trace(code, loc)) {
                        if (code == Bytecodes::_jportal_bytecode) {
                            const u1* codes;
                            size_t size;
                            size_t new_loc;
                            if (!trace->get_inter(loc, codes, size, new_loc)) {
                                fprintf(stderr, "Decode output: cannot get inter.\n");
                                return;
                            }
                            // output bytecode
                            for (int i = 0; i < size; i++) {
                                fprintf(fp, "%hhu\n", *(codes+i));
                            }
                            access.set_current(new_loc);
                        } else if (code == Bytecodes::_jportal_jitcode_entry ||
                                        code == Bytecodes::_jportal_jitcode) {
                            const jit_section *section = nullptr;
                            const PCStackInfo **pcs = nullptr;
                            size_t size;
                            size_t new_loc;
                            if (!trace->get_jit(loc, pcs, size, section, new_loc)
                                    || !pcs || !section) {
                                fprintf(stderr, "Decode output: cannot get jit.\n");
                                return;
                            }
                            // output jit
                            const CompiledMethodDesc *cmd = section->cmd;
                            if (!cmd) {
                                access.set_current(new_loc);
                                continue;
                            }
                            string klass_name, name, sig;
                            for (int i = 0; i < size; i++) {
                                const PCStackInfo *pc = pcs[i];
                                int bci = pc->bcis[0];
                                int mi = pc->methods[0];
                                if (!cmd->get_method_desc(mi, klass_name, name, sig)) {
                                    fprintf(stderr, "Decode output: no method fo method index.\n");
                                    continue;
                                }
                                const Klass *klass;
                                const Method *method;
                                klass = analyser->getKlass(klass_name);
                                if (!klass)
                                    continue;
                                    method = klass->getMethod(name+sig);
                                if (!method)
                                    continue;
                                auto res = method_map.find(method);
                                if (res == method_map.end())
                                    fprintf(fp, "-1\n");
                                else
                                    fprintf(fp, "%d\n", res->second);
                            }
                            access.set_current(new_loc);
                        }
                    }
                }
            }
            iter2++;
            iter3++;
        }
        fclose(fp);
        iter1++;
    }
}

void decode(const char *trace_data, list<TraceData*> &traces,
                Analyser *analyser) {
    map<int, list<TracePart>> traceparts;
    int errcode;
    errcode = ptjvm_split(trace_data, traceparts);
    if (errcode < 0)
        return;

    const int MaxThreadCount = 8;
    bool ThreadState[MaxThreadCount]{false};
    ///* Create TaskManager *///
    TaskManager* tm = new TaskManager();

    for (auto part1 : traceparts) {
        for (auto part2 : part1.second) {
            TraceData *trace = new TraceData();
            TraceDataRecord record(*trace);
            traces.push_back(trace);
            
            DecodeTask *task = new DecodeTask(part2, *trace, analyser);
            tm->commitTask(task);
        }
    }
    ///* Create Workers *///
    Worker w(&(ThreadState[0]), tm);
    new std::thread(w);

    while (true) {
        int worker_count = 0;
        for (int i = 0; i < MaxThreadCount; ++i) {
            if (ThreadState[i]) {
                ++worker_count;
            } else if (tm->isNeedMoreWorker()) {
                Worker w(&(ThreadState[i]), tm);
                new std::thread(w);
                ++worker_count;
            }
        }
        if (worker_count==0)
            break;
    }

    // decode_output(traceparts, traces, analyser);
}

bool cmp(pair<ThreadSplit, MethodMatcher *> x, 
          pair<ThreadSplit, MethodMatcher *> y) {
    if (x.first.start_time < y.first.start_time
        || x.first.start_time == y.first.start_time && 
            x.first.end_time < y.first.end_time)
        return true;
    return false;
}

void match(Analyser *analyser, list<TraceData*> &traces) {
    const int MaxThreadCount = 8;
    bool ThreadState[MaxThreadCount]{false};
    ///* Create TaskManager *///
    TaskManager* tm = new TaskManager();
    for (auto trace : traces) {
        if (!trace)
            continue;
        unordered_map<long, list<ThreadSplit>> &thread_map = trace->get_thread_map();
        for (auto thread : thread_map) {
            for (auto split : thread.second) {
                MethodMatcher *matcher = new MethodMatcher(*analyser, *trace,
                                split.start_addr, split.end_addr);
                MatchTask *task = new MatchTask(matcher, thread.first, split);
                tm->commitTask(task);
            }
        }
    }
    ///* Create Workers *///
    Worker w(&(ThreadState[0]), tm);
    new std::thread(w);

    while (true) {
        int worker_count = 0;
        for (int i = 0; i < MaxThreadCount; ++i) {
            if (ThreadState[i]) {
                ++worker_count;
            } else if (tm->isNeedMoreWorker()) {
                Worker w(&(ThreadState[i]), tm);
                new std::thread(w);
                ++worker_count;
            }
        }
        if (worker_count==0)
            break;
    }

    auto iter = MatchTask::threads.begin();
    for (; iter != MatchTask::threads.end(); iter++) {
        sort(iter->second.begin(), iter->second.end(), cmp);
    }
}

int main(int argc, char **argv) {
    char defualt_trace[20] = "JPortalTrace.data";
    char default_dump[20] = "JPortalDump.data"; 
    char *dump_data = default_dump;
    char *trace_data = defualt_trace;
    char *class_config = nullptr;
    char *callback = nullptr;
    int errcode, i;
    for (i = 1; i < argc;) {
        char *arg;
        arg = argv[i++];

        if (strcmp(arg, "--trace-data") == 0) {
            if (argc <= i) {
                return -1;
            }
            trace_data = argv[i++];
            continue;
        }

        if (strcmp(arg, "--dump-data") == 0) {
            if (argc <= i) {
                return -1;
            }
            dump_data = argv[i++];
            continue;
        }

        if (strcmp(arg, "--class-config") == 0) {
            if (argc <= i) {
                return -1;
            }
            class_config = argv[i++];
            continue;
        }

        if (strcmp(arg, "--callback") == 0) {
            if (argc <= i) {
                return -1;
            }
            callback = argv[i++];
            continue;
        }
        fprintf(stderr, "unknown:%s\n", arg);
        return -1;
    }

    if (!class_config) {
        fprintf(stderr, "Please specify class config:--class-config\n");
        return -1;
    }
    if (!trace_data) {
        fprintf(stderr, "Please specify trace data:--trace-data\n");
        return -1;
    }
    if (!dump_data) {
        fprintf(stderr, "Please specify dump data:--trace-data\n");
        return -1;
    }

    ///* Initializing *///
    std::cout<<"Initializing..."<<std::endl;
    Bytecodes::initialize();
    JvmDumpDecoder::initialize(dump_data);
    Analyser *analyser = new Analyser(class_config);
    analyser->analyse_all();
    if (callback)
        analyser->analyse_callback(callback);
    std::cout<<"Initializing completed."<<std::endl;

    ///* Decoding *///
    std::cout<<"Decoding..."<<std::endl;
    list<TraceData*> traces;
    decode(trace_data, traces, analyser);
    std::cout<<"Decoding completed."<<std::endl;

    ///* Matching *///
    std::cout<<"Matching..."<<std::endl; 
    match(analyser, traces);
    std::cout<<"Matching completed."<<std::endl;

    ///* Output *///
    analyser->output_method_map();
    for (auto thread : MatchTask::threads) {
        char thread_name[256];
        sprintf(thread_name, "%ld", thread.first);
        FILE *fp = fopen(thread_name, "w");
        for (auto matcher : thread.second) {
            if (!matcher.second || matcher.second->empty())
                continue;
            fprintf(fp, "#%lld %lld %d %d\n", matcher.first.start_time,
                matcher.first.end_time, matcher.first.head_loss, matcher.first.tail_loss);
            matcher.second->output(fp);
        }
        fclose(fp);
    }

    ///* Exit *///
    for (auto trace : traces) {
        delete trace;
    }
    for (auto thread : MatchTask::threads) {
        for (auto matcher : thread.second)
            delete matcher.second;
    }
    JvmDumpDecoder::destroy();

    return 0;
}