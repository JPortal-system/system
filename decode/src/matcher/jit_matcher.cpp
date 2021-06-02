#include "matcher/jit_matcher.hpp"
#include "structure/java/classFileParser.hpp"

bool JitMatcher::match(const Method *method, int bci) {
    if (!method)
        return false;

    vector<pair<int, int>> result;
    BlockGraph *bg = method->get_bg();

    auto bct_iter = bg->bct_offset.find(bci);
    if (bct_iter == bg->bct_offset.end())
        return false;
    int bct_offset = bct_iter->second;
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

bool JitMatcher::match(const Method *method, int src, int dest) {
    if (!method)
        return false;

    BlockGraph *bg = method->get_bg();

    auto bct_iter = bg->bct_offset.find(src);
    if (bct_iter == bg->bct_offset.end())
        return false;
    int bct_offset = bct_iter->second;
    int block_id = bg->_block_id[bct_offset];
    Block *block = bg->_blocks[block_id];

    bct_iter = bg->bct_offset.find(dest);
    if (bct_iter == bg->bct_offset.end())
        return true;
    bct_offset = bct_iter->second;
    block_id = bg->_block_id[bct_offset];
    Block *dest_block = bg->_blocks[block_id];

    while (block->get_succs_size() == 1 && block != dest_block) {
        auto iter = block->get_succs_begin();
        block = *iter;
    }

    if (block == dest_block)
        return true;

    for (auto iter = block->get_succs_begin();
            iter != block->get_succs_end();
            ++iter) {
        Block *new_block = *iter;
        while (new_block->get_succs_size() == 1 && new_block != dest_block) {
            auto iter_p = new_block->get_succs_begin();
            new_block = *iter_p;
        }
        if (new_block == dest_block) {
            while (new_block->get_succs_size() == 1) {
                auto iter_p = new_block->get_succs_begin();
                new_block = *iter_p;
            }
            return true;
        }
    }
    return false;
}

bool JitMatcher::will_return(const Method *method, int bci) {
    if (!method)
        return true;

    BlockGraph *bg = method->get_bg();

    auto iter = bg->bct_offset.find(bci);
    if (iter == bg->bct_offset.end())
        return true;
    int bct_offset = iter->second;
    int block_id = bg->_block_id[bct_offset];
    Block *block = bg->_blocks[block_id];

    while (block->get_succs_size() == 1) {
        int bct_codebegin = block->get_bct_codebegin();
        int bctsize = block->get_bctsize();
        auto iter = block->get_succs_begin();
        block = *iter;
    }
    if (block->get_succs_size() == 0)
        return true;
    return false;
}

bool JitMatcher::is_entry(const Method *method, int bci) {
    return match(method, 0, bci);
}