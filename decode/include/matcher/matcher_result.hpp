#ifndef MATCHER_RESULT_HPP
#define MATCHER_RESULT_HPP

#include "structure/java/method.hpp"
#include "structure/common.hpp"

class MatcherResult {
  private:
    unordered_map<size_t, unordered_map<const Method*, pair<pair<int, int>, size_t>>> success;
    unordered_map<size_t, pair<vector<const Method *>, pair<int, size_t>>> jit;
    unordered_map<size_t, size_t> no_context;

  public:

    bool getMatchResult(size_t &addr, const Method* method, int &score, int depth);

    bool getMatchJitResult(size_t &addr, int &score);

    void setSuccResult(size_t addr, const Method* method, int score, int depth, size_t new_addr);

    void setJitResult(size_t addr, vector<const Method *> &methods, int score, size_t new_addr);

    bool getNoContextMatchResult(size_t addr, size_t &new_addr);

    void setNoContextMatchResult(size_t addr, size_t new_addr);

    unordered_map<size_t, unordered_map<const Method*, pair<pair<int, int>, size_t>>> &getSuccess() { return success; }
    unordered_map<size_t, pair<vector<const Method *>, pair<int, size_t>>> &getJit() { return jit; };
    bool empty() { return success.empty() && jit.empty(); }
};

#endif // MATCHER_RESULT_HPP