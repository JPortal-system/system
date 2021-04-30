#include "matcher/block_matcher.hpp"

bool BlockMatcher::match_positive(BlockGraph *bg, BCTBlockList *bctBlockList, int bctBlockId,
                int code_offset, vector<int> &offsets, vector<vector<pair<int, int>>> &result) {
    if (!bg || !bctBlockList || bctBlockId < 0)
        return false;

    bg->build_bctlist();
    bg->build_graph();

    bctBlockList->build_list();

    if (code_offset < 0 || code_offset >= bg->_code_count)
        return false;
    if (!bg->contain_bc_set(bctBlockList->get_bc_set()))
        return false;
    
    bool matchVal = false;
    Block *block = bg->_blocks[bg->_block_id[code_offset]];
    int blc_bctoffset = code_offset-block->get_bct_codebegin();
    vector<pair<int, int>> single_result;
    int single_offset = code_offset;
    auto bctBlocks = bctBlockList->get_blocks();
    while (bctBlockId < bctBlocks->size()) {
        BCTBlock *bctblock = (*bctBlocks)[bctBlockId];
        // bctblock:[begin, end) == block:[begin_0, end_0)
        //                               +[begin_1, end_1)
        //                               +...
        //                               +[begin_n, end_n)
        int bctblock_size = bctblock->get_size();
        int block_bctsize = block->get_bctsize();
        int offset = 0;
        // [begin_0, end_0)+...+[begin_n-1, end_n-1)
        while (bctblock_size - offset > block_bctsize - blc_bctoffset) {
            if (!bctblock->is_include_positive(offset, block->get_bctblock(), blc_bctoffset))
                return false;
            if (!(1 == block->get_succs_size()))
                return false;
            code_offset = single_offset+block->get_bctsize()-blc_bctoffset;
            single_result.push_back(make_pair(single_offset, code_offset));
            single_offset = code_offset;
            offset += (block_bctsize - blc_bctoffset);
            // get block succs
            auto iter = block->get_succs_begin();
            block = *(iter);
            block_bctsize = block->get_bctsize();
            blc_bctoffset = 0;
        }
        // bct block ends
        if (-1 == bctblock->get_branch()) {
            if (!bctblock->is_part_of_positive(offset, block->get_bctblock(), blc_bctoffset))
                return false;
            code_offset = single_offset+bctblock->get_size()-offset;
            single_result.push_back(make_pair(single_offset, code_offset));
            single_offset = code_offset;
            offsets.push_back(single_offset);
            result.push_back(single_result);
            return true;
        }
        // bctblock:[begin+offset, end) == block:[begin_n,end_n)
        // switch
        else if (2 == bctblock->get_branch()) {
            if (!bctblock->is_include_positive(offset, block->get_bctblock(), blc_bctoffset))
                return false;
            code_offset = single_offset+block->get_bctsize()-blc_bctoffset;
            single_result.push_back(make_pair(single_offset, code_offset));
            single_offset = code_offset;
            // get block succs
            auto iter = block->get_succs_begin();
            auto iter_end = block->get_succs_end();
            blc_bctoffset = 0;
            for (; iter != iter_end; ++iter) {
                int switch_code_offset = (*iter)->get_bct_codebegin();
                vector<vector<pair<int, int>>> switch_result;
                if (match_positive(bg, bctBlockList, bctBlockId+1, switch_code_offset,
                                    offsets, switch_result)) {
                    matchVal = true;
                    for (auto single_switch = switch_result.begin();
                            single_switch != switch_result.end();
                            single_switch++) {
                        single_switch->insert(single_switch->begin(),
                                single_result.begin(), single_result.end());
                        result.push_back(*single_switch);
                    }
                }
            }
            return matchVal;
        }
        // bctblock:[begin+offset, end) == block:[begin_n,end_n)
        // others
        else {
            if (!bctblock->is_include_positive(offset, block->get_bctblock(), blc_bctoffset))
                return false;
            code_offset = single_offset+block->get_bctsize()-blc_bctoffset;
            single_result.push_back(make_pair(single_offset, code_offset));
            single_offset = code_offset;
            // get block succs
            auto iter = block->get_succs_begin();
            auto iter_end = block->get_succs_end();
            if (iter == iter_end) {
                offsets.push_back(single_offset);
                result.push_back(single_result);
                return true;
            }
            if (1 == bctblock->get_branch())
                ++iter;
            if (iter == iter_end) {
                return false;
            }
            block = *(iter);
            blc_bctoffset = 0;
            single_offset = block->get_bct_codebegin();
        }
        bctBlockId++;
    }
    return matchVal;
}

bool BlockMatcher::match_exception(BlockGraph *bg, BCTBlockList *bctBlockList,
                vector<int> &offsets, vector<vector<pair<int, int>>> &result) {
    if (!bg)
        return false;
    bool matchVal = false;
    for (Excep ecp : *(bg->get_exceps())) {
        vector<vector<pair<int, int>>> exception_result;
        auto iter = bg->bct_offset.find(ecp.target);
        if (iter == bg->bct_offset.end())
            continue;
        if (match_positive(bg, bctBlockList, 0, (*iter).second, offsets, exception_result)) {
            result.insert(result.end(), exception_result.begin(), exception_result.end());
            matchVal = true; 
        }
    }
    return matchVal;
}

bool BlockMatcher::match(const Method *method, vector<BCTBlockList *> &bctBlocklists,
                int start_offset, vector<vector<int>> &offsets) {
    if (!method)
        return false;

    BlockGraph *bg = method->get_bg();
    vector<int> code_offsets;
    code_offsets.push_back(start_offset);
    for (auto bct : bctBlocklists) {
        if (!bct)
            return false;
        bool matchVal = false;
        vector<int> round_offsets;
        if (bct->get_exception()) {
            vector<vector<pair<int, int>>> single_result;
            if (match_exception(bg, bct, round_offsets, single_result)) {
                matchVal = true;
            }
        } else {
            for (auto code_offset : code_offsets) {
                vector<int> single_offsets;
                vector<vector<pair<int, int>>> single_result;
                if (match_positive(bg, bct, 0, code_offset, single_offsets, single_result)) {
                    matchVal = true;
                    for (auto single_offset : single_offsets) {
                        bool memorized = false;
                        for (auto round_offset : round_offsets) {
                            if (round_offset == single_offset) {
                                memorized = true;
                                break;
                            }
                        }
                        if (!memorized)
                            round_offsets.push_back(single_offset);
                    }
                }
            }
        }
        if (!matchVal)
            return false;
        code_offsets = round_offsets;
        offsets.push_back(code_offsets);
    }
    return true;
}