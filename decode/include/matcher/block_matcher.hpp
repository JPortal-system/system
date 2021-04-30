#ifndef BLOCK_MATCHER_HPP
#define BLOCK_MATCHER_HPP

#include "structure/java/classFileParser.hpp"

class BlockMatcher {
private:
    static bool match_positive(BlockGraph *bg, BCTBlockList *bctBlockList, int bctBlockId,
                int code_offset, vector<int> &offsets, vector<vector<pair<int, int>>> &result);
    static bool match_exception(BlockGraph *bg, BCTBlockList *bctBlockList,
                    vector<int> &offsets, vector<vector<pair<int, int>>> &result);
public:
    static bool match(const Method *method, vector<BCTBlockList *> &bctBlocklists,
                        int start_offset, vector<vector<int>> &offsets);
    
};

#endif // BLOCK_MATCHER_HPP