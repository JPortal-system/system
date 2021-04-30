#include "matcher/matcher_result.hpp"

bool MatcherResult::getMatchResult(size_t &addr, const Method* method, int &score, int depth) {
    auto iter1 = success.find(addr);
    if(iter1 == success.end()) return false;
    auto iter2 = iter1->second.find(method);
    if(iter2 == iter1->second.end()) return false;

    if (iter2->second.first.second < depth)
        iter2->second.first.second = depth;
    score = iter2->second.first.first;
    addr = iter2->second.second;
    return true;
}

bool MatcherResult::getMatchJitResult(size_t &addr, int &score) {
    auto iter = jit.find(addr);
    if (iter == jit.end())
        return false;
    score = iter->second.second.first;
    addr = iter->second.second.second;
    return true;
}

// new_score = score
void MatcherResult::setSuccResult(size_t addr, const Method* method, int score, int depth, size_t new_addr){
    if (success.find(addr)==success.end()) {
        success[addr] = unordered_map<const Method*, pair<pair<int, int>, size_t>>();
    }
    success[addr][method] = make_pair(make_pair(score, depth), new_addr);
}

void MatcherResult::setJitResult(size_t addr, vector<const Method *> &methods, int score, size_t new_addr) {
    jit[addr] = make_pair(methods, make_pair(score, new_addr));
    return;
}

bool MatcherResult::getNoContextMatchResult(size_t addr, size_t &new_addr) {
    auto iter = no_context.find(addr);
    if (iter == no_context.end())
        return false;
    new_addr = iter->second;
    return true;
}

void MatcherResult::setNoContextMatchResult(size_t addr, size_t new_addr) {
    no_context[addr] = new_addr;
}