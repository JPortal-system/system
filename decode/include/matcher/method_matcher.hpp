#ifndef METHOD_MATCHER_HPP
#define METHOD_MATCHER_HPP

#include "matcher_result.hpp"
#include "structure/java/analyser.hpp"
#include "structure/PT/decode_result.hpp"

class MethodMatcher {
  public:
    struct MatchedMethod {
      const Method *method;
      bool jit;
      int bci;
      const jit_section *section = nullptr;
      MatchedMethod(const Method *_method, bool _jit, int _bci) :
        method(_method), jit(_jit), bci(_bci) {}
      MatchedMethod(const Method *_method, bool _jit, int _bci,
                      const jit_section *_section) :
        method(_method), jit(_jit), bci(_bci), section(_section) {}
    };
  private:
    MatcherResult result;
    TraceData &trace;
    size_t start_addr;
    size_t end_addr;
    Analyser &analyser;
    unordered_map<size_t, pair<BCTBlockList *, size_t>> bct_map;
    stack<stack<MatchedMethod>> mstack;
    int match_jit(size_t &addr,size_t terminal, bool entry, int depth);
    int match_caller(size_t &addr, const Method* context, int depth);
    int match_callee(size_t &addr, const Method* context, int offset, int depth);
    int match_dispatch(TraceDataAccess access, size_t &addr,
                        const Method *method, vector<int> &callSites, int depth);
    int match(size_t &addr, const Method* candidates, int offset, int depth);
    void match(size_t start, size_t end);
  public:
    MethodMatcher(Analyser &_analyser, TraceData &_trace,
                  size_t _start_addr, size_t _end_addr) :
        analyser(_analyser),
        trace(_trace),
        start_addr(_start_addr),
        end_addr(_end_addr) {}
    ~MethodMatcher() {
      for (auto bct : bct_map) {
        delete bct.second.first;
      }
    }
    void match();
    void output(FILE *fp);
    bool empty() { return result.empty(); }
};

#endif // METHOD_MATCHER_HPP