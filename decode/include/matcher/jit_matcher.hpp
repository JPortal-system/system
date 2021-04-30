#ifndef JIT_MATCHER_HPP
#define JIT_MATCHER_HPP

#include "structure/java/method.hpp"

class JitMatcher {
private:

public:
    static bool match(const Method *method, int bci);
};

#endif