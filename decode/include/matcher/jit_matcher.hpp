#ifndef JIT_MATCHER_HPP
#define JIT_MATCHER_HPP

#include "structure/java/method.hpp"

class JitMatcher {
private:

public:
    static bool match(const Method *method, int bci);
    static bool match(const Method *method, int src, int dest);
    static bool will_return(const Method *method, int bci);
    static bool is_entry(const Method *, int bci);
};

#endif