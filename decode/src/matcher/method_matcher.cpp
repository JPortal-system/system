#include "matcher/method_matcher.hpp"
#include "matcher/block_matcher.hpp"
#include "matcher/jit_matcher.hpp"

#include <algorithm>

int MethodMatcher::match_jit(size_t &addr, size_t terminal, bool entry, int depth) {
    const jit_section *section = nullptr;
    const PCStackInfo **pcs = nullptr;
    size_t length;

    int score;
    if (result.getMatchJitResult(addr, score))
        return score;

    size_t new_addr;
    if (!trace.get_jit(addr, pcs, length, section, new_addr) || !pcs || !section) {
        fprintf(stderr, "Method Match: cannot get jit.\n");
        return 0;
    }
    if (!entry && depth > 0 && !mstack.top().empty() &&
            mstack.top().top().section != section) {
        return 0;
    }
    const CompiledMethodDesc *cmd = jit_section_cmd(section);
    if (!cmd) {
        addr = new_addr;
        return 0;
    }
    int sum = 0;
    vector<const Method *> jit_result;
    const Klass *klass = nullptr;
    const Method *method = nullptr;
    int bci = -1;
    int mi = 0;
    for (int i = 0; i < length; i++) {
        const PCStackInfo *pc = pcs[i];
        int bci = pc->bcis[0];
        int mi = pc->methods[0];
        string klass_name, name, sig;
        if (!cmd->get_method_desc(mi, klass_name, name, sig)) {
            fprintf(stderr, "Method Match: no method fo method index.\n");
            continue;
        }
        klass = analyser.getKlass(klass_name);
        if (!klass)
            continue;
        method = klass->getMethod(name+sig);
        if (!method)
            continue;
        if (mstack.top().empty() || !mstack.top().top().jit || entry) {
            if (bci == -1)
                bci = 0;
            if (JitMatcher::match(method, bci)) {
                jit_result.push_back(method);
                mstack.top().push(MatchedMethod(method, true, bci, section));
            }
            entry = false;
        } else {
            if (mstack.top().top().method == method) {
                if (bci == -1) {
                    continue;
                } else if (JitMatcher::match(method,
                        mstack.top().top().bci, bci)) {
                    mstack.top().top().bci = bci;
                    continue;
                } else if (!JitMatcher::is_entry(method, bci)) {
                    mstack.top().top().bci = bci;
                    continue;
                }
            }
            bool has_popped = false;
            while (!mstack.top().empty() && mstack.top().top().jit &&
                    mstack.top().top().method != method &&
                    JitMatcher::will_return(mstack.top().top().method,
                        mstack.top().top().bci) ) {
                mstack.top().pop();
                has_popped = true;
            }
            if (has_popped &&!mstack.top().empty() &&
                    mstack.top().top().jit &&
                    mstack.top().top().method == method) {
                if (bci == -1) {
                    continue;
                } else if (JitMatcher::match(method,
                        mstack.top().top().bci, bci)) {
                    mstack.top().top().bci = bci;
                    continue;
                } else if (!JitMatcher::is_entry(method, bci)) {
                    mstack.top().top().bci = bci;
                    continue;
                }
            }
            if (bci == -1)
                bci = 0;
            if (JitMatcher::match(method, bci)) {
                jit_result.push_back(method);
                mstack.top().push(MatchedMethod(method, true, bci, section));
            }
        }
    }
    TraceDataAccess access(trace, new_addr, terminal);
    vector<int> CallSites;
    CallSites.push_back(bci);
    sum += match_dispatch(access, new_addr, method, CallSites, depth+1);
    result.setJitResult(addr, jit_result, sum, new_addr);
    addr = new_addr;
    return sum;
}

// caller match
int MethodMatcher::match_caller(size_t &addr, const Method* context, int depth) {
    bool hasContext = true;
    const list<pair<int, const Method*>> *candidates = nullptr;
    if (context)
        candidates = context->get_caller_list();
    if (!candidates || candidates->empty()) {
        size_t new_addr;
        if (result.getNoContextMatchResult(addr, new_addr)) {
            addr = new_addr;
            return 0;
        }
        hasContext = false;
        candidates = analyser.get_all_call_sites();
    }

    int retVal = 0;
    size_t retAddr = addr;
    for (auto callsite: *candidates) {
        size_t tempAddr = addr;
        mstack.top().push(MatchedMethod(callsite.second, false, callsite.first));
        int temp = match(tempAddr, callsite.second, callsite.first, depth);
        if (temp > retVal) {
            retVal = temp;
            retAddr = tempAddr;
        }
        mstack.top().pop();
    }
    if (hasContext) {
        addr = retAddr;
        return retVal;
    } else {
        result.setNoContextMatchResult(addr, retAddr);
        addr = retAddr;
        return 0;
    }
}

// callee match
int MethodMatcher::match_callee(size_t &addr, const Method* context, int offset, int depth) {
    bool hasContext = true;
    const list<const Method *> *candidates = nullptr;
    bool is_get_all=false;
    bool is_call_back =false;

    if (context)
        candidates = context->get_callee_list(offset);
    if (!candidates || candidates->empty()) {
        size_t new_addr;
        if (result.getNoContextMatchResult(addr, new_addr)) {
            addr = new_addr;
            return 0;
        }
        hasContext = false;
        if (depth == 0) {
            candidates = analyser.get_all_methods();
            is_get_all=true;

        } else {
            candidates = analyser.get_callbacks();
            is_call_back=true;
        }
    }

    int retVal = 0;
    size_t retAddr = addr;
    for (auto callsite: *candidates) {
        size_t tempAddr = addr;
        mstack.top().push(MatchedMethod(callsite, false, 0));
        int temp = match(tempAddr, callsite, 0, depth);
        if (temp > retVal) {
            retVal = temp;
            retAddr = tempAddr;
        }
        mstack.top().pop();
    }

    if (hasContext) {
        addr = retAddr;
        return retVal;
    } else {
        if(is_get_all)
            result.setNoContextMatchResult(addr, retAddr);
        addr = retAddr;
        return 0;
    }
}

// match dispatch
int MethodMatcher::match_dispatch(TraceDataAccess access, size_t &addr,
                    const Method *method, vector<int> &callSites, int depth) {
    int retVal = 0;
    size_t retAddr;
    Bytecodes::Code code;
    bool isCallee;

    if (!access.next_trace(code, addr))
        return 0;
    if (code == Bytecodes::_jportal_method_entry) {
        isCallee = true;
    } else if (code == Bytecodes::_jportal_invoke_return_entry_points) {
        isCallee = false;
    } else if (code == Bytecodes::_jportal_jitcode) {
        if (depth != 0)
            return 0;
        return match_jit(addr, access.get_end(), false, depth);
    } else if (code == Bytecodes::_jportal_jitcode_entry) {
        if (depth != 0)
            return 0;
        return match_jit(addr, access.get_end(), true, depth);
    } else {
        addr = access.get_current();
        return 0;
    }

    if (!access.next_trace(code, addr))
        return 0;

    retAddr = addr;
    if (code == Bytecodes::_jportal_bytecode) {
        TraceData::SplitKind kind = trace.get_split_kind(addr);
        if (kind == TraceData::_NOT_SPLIT)
            return 0;
        if (!isCallee)
            return match_caller(addr, method, depth);
        if (!method)
            return match_callee(addr, nullptr, -1, depth);

        for (auto callee : callSites) {
            size_t loc = addr;
            int temp = match_callee(loc, method, callee, depth);
            if (temp > retVal) {
                retAddr = loc;
                retVal = temp;
            }
        }
    }
    addr = retAddr;
    return retVal;
}

// call Blockmatcher::match
int MethodMatcher::match(size_t &addr, const Method* method, int offset, int depth) {
    int score;
    if (result.getMatchResult(addr, method, score, depth))
        return score;

    MethodDesc md;
    if (trace.get_md(addr, md) && offset == 0) {
        string klass_name, name, sig;
        md.get_method_desc(klass_name, name, sig);
        const Klass *rklass = analyser.getKlass(klass_name);
        if (rklass) {
            const Method *rmethod = rklass->getMethod(name+sig);
            if (rmethod && rmethod != method) {
                match(addr, rmethod, 0, 0);
                return 0;
            }
        }
    }

    vector<size_t> interList;
    if (!trace.get_inter(addr, interList)) {
        fprintf(stderr, "Method Match: cannot get inter.\n");
        return 0;
    }

    vector<BCTBlockList *> bctList;
    vector<pair<size_t, size_t>> addrList;
    for (auto single : interList) {
        auto iter = bct_map.find(single);
        if (iter == bct_map.end()) {
            const u1* codes;
            size_t size;
            size_t new_addr;
            if (!trace.get_inter(single, codes, size, new_addr)) {
                fprintf(stderr, "Method Match: cannot get inter.\n");
                return 0;
            }
            BCTBlockList *bct = new BCTBlockList(codes, size);
            bct_map[single] = make_pair(bct, new_addr);
            bctList.push_back(bct);
            if (!addrList.empty())
                addrList.back().second = single;
            addrList.push_back(make_pair(new_addr, end_addr));
        } else {
            bctList.push_back(iter->second.first);
            if (!addrList.empty())
                addrList.back().second = single;
            addrList.push_back(make_pair(iter->second.second, end_addr));
        }
    }
    bool isMatch = false;
    vector<vector<int>> callSites;
    isMatch = BlockMatcher::match(method, bctList, offset, callSites);
    if (callSites.size() > addrList.size()) {
        fprintf(stderr, "MethodMatch: block match error.\n");
        return 0;
    }

    int sum = 0;
    auto addrIter = addrList.begin();
    size_t loc = addr;
    for (auto callees: callSites) {
        TraceDataAccess access(trace, addrIter->first, addrIter->second);
        size_t current_size = mstack.top().size();
        sum += match_dispatch(access, loc, method, callees, depth+1);
        while(mstack.top().size() > current_size)
            mstack.top().pop();
        size_t end = addrIter->second;
        addrIter++;
        // match remaining
        if (addrIter != addrList.end() && loc < end) {
            match(loc, end);
        }
    }
    if (isMatch) {
        result.setSuccResult(addr, method, sum+1, depth, loc);
        addr = loc;
        return sum + 1;
    }
    addr = loc;
    return 0;
}

void MethodMatcher::match(size_t start, size_t end) {
    TraceDataAccess access(trace, start, end);
    size_t addr = start;
    mstack.push(stack<MatchedMethod>());
    while (!access.end()) {
        vector<int> callSites;
        match_dispatch(access, addr, nullptr, callSites, 0);
        access.set_current(addr);
    }
    mstack.pop();
}

void MethodMatcher::match() {
    match(start_addr, end_addr);
}

void MethodMatcher::output(FILE *fp) {
    if (!fp)
        return;

    const unordered_map<const Method *, int> &method_map = analyser.get_method_map();
    unordered_map<size_t, std::unordered_map<const Method *,
            pair<pair<int, int>, size_t>>>& success = result.getSuccess();
    unordered_map<size_t, std::pair<std::vector<const Method *>,
            std::pair<int, size_t>>> &jit = result.getJit();
    vector<size_t> ordered;
    for (auto res = success.begin(); res != success.end(); res++) {
        ordered.push_back(res->first);
    }
    for (auto res = jit.begin(); res != jit.end(); res++) {
        ordered.push_back(res->first);
    }

    sort(ordered.begin(), ordered.end());
    for (auto res : ordered) {
        auto iter1 = success.find(res);
        if (iter1 != success.end()) {
            // inter
            int method_index = 0, score = -1, depth = -1;
            bool single = true;
            for (auto pres: iter1->second) {
                if (pres.second.first.first > score || pres.second.first.first == score
                        && pres.second.first.second > depth) {
                    score = pres.second.first.first;
                    depth = pres.second.first.second;
                    auto iter = method_map.find(pres.first);
                    if (iter == method_map.end())
                        method_index = 0;
                    else
                        method_index = iter->second;
                    single = true;
                } else if (pres.second.first.first == score
                        && pres.second.first.second == depth) {
                    single = false;
                }
            }
            if (single)
                fprintf(fp, "%d\n", method_index);
            continue;
        }
        // jit
        auto iter2 = jit.find(res);
        if (iter2 == jit.end())
            continue;
        for (auto single : iter2->second.first) {
            int method_index = 0;
            auto iter = method_map.find(single);
            if (iter != method_map.end())
                method_index = iter->second;
            fprintf(fp, "%d\n", method_index);
        }
    }
}