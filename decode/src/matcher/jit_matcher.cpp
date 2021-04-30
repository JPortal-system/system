#include "matcher/jit_matcher.hpp"
#include "structure/java/classFileParser.hpp"

bool JitMatcher::match(const Method *method, int bci) {
    if (!method)
        return false;

    vector<pair<int, int>> result;
    BlockGraph *bg = method->get_bg();

    auto iter = bg->bct_offset.find(bci);
    if (iter == bg->bct_offset.end())
        return false;
    int bct_offset = iter->second;
    int block_id = bg->_block_id[bct_offset];
    Block *block = bg->_blocks[block_id];

    while (block->get_succs_size() == 1) {
        int bct_codebegin = block->get_bct_codebegin();
        int bctsize = block->get_bctsize();
        result.push_back(make_pair(bct_codebegin, bct_codebegin + bctsize));
        auto iter = block->get_succs_begin();
        block = *iter;
    }
    return true;
}
