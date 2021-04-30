#ifndef JAVA_BLOCK_HPP
#define JAVA_BLOCK_HPP

#include "type_defs.hpp"
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

//
class BCTBlock {
  private:
    // BCTBlockList::_blocks[_block_id]
    int _block_id;
    // [_begin, _end)
    const u1 *_begin;
    const u1 *_end;
    // branch:
    //  -1: exception/return
    //  0:  block._succs[0]
    //  1:  block._succs[1]
    //  2:  switch
    int _branch;
  public:
    BCTBlock(int id, const u1 *begin)
        : _block_id(id), _begin(begin), _end(nullptr), _branch(0){};
    ~BCTBlock(){};
    void set_end(const u1 *end) { _end = end; }
    void set_branch(int branch) { _branch = branch; }
    const u1 *get_begin() { return _begin; }
    const u1 *get_end() { return _end; }
    int get_id() { return _block_id; }
    int get_branch() const { return _branch; }
    int get_size() const { return _end - _begin; }
    bool is_part_of_positive(int offset, BCTBlock *bctblock, int blc_bctoffset) {
        int src_len = _end - _begin - offset;
        int dest_len = bctblock->_end - bctblock->_begin - blc_bctoffset;
        if (src_len > dest_len || src_len < 0)
            return false;
        return (0 == memcmp(_begin + offset, bctblock->_begin + blc_bctoffset, src_len));
    }
    bool is_include_positive(int offset, BCTBlock *bctblock, int blc_bctoffset) {
        int dest_len = bctblock->_end - bctblock->_begin - blc_bctoffset;
        if (dest_len < 0 || _begin + offset + dest_len > _end)
            return false;
        return (0 == memcmp(_begin + offset, bctblock->_begin + blc_bctoffset, dest_len));
    }
    bool is_part_of_reverse(int offset, BCTBlock *bctblock, int blc_bctoffset) {
        int src_len = offset;
        int dest_len = bctblock->_end - bctblock->_begin - blc_bctoffset;
        if (src_len < 0 || src_len > dest_len)
            return false;
        return (0 == memcmp(_begin, bctblock->_end - offset, src_len));
    }
    bool is_include_reverse(int offset, BCTBlock *bctblock, int blc_bctoffset) {
        int dest_len = bctblock->_end - bctblock->_begin - blc_bctoffset;
        if (dest_len < 0 || _end - offset - dest_len < _begin)
            return false;
        return (0 == memcmp(_end - offset - dest_len, bctblock->_begin, dest_len));
    }
    bool is_equal(BCTBlock *bctblock) {
        // length
        int src_len = _end - _begin;
        int dest_len = bctblock->_end - bctblock->_begin;
        if (src_len != dest_len) {
            return false;
        }
        // bytecodes
        return (0 == memcmp(_begin, bctblock->_begin, src_len));
    }
};
//
class Block {
  private:
    // BlockGraph::_blocks[_block_id]
    int _block_id;
    // [_begin_offset, _end_offset)
    int _begin_offset;
    int _end_offset;
    int _bct_codebegin;
    vector<Block *> _preds;
    vector<Block *> _succs;
    unordered_set<Block *> _preds_set;
    unordered_set<Block *> _succs_set;
    BCTBlock *_bctblock = nullptr;

  public:
    Block(int id, int begin_offset, int bct_codebegin)
        : _block_id(id), _begin_offset(begin_offset), _end_offset(-1), _bct_codebegin(bct_codebegin) {}
    ~Block() {}
    void set_end_offset(int offset) { _end_offset = offset; }
    void set_bctblock(BCTBlock *bctblock) { _bctblock = bctblock; }
    void add_preds(Block *block) {
        if (_preds_set.find(block) == _preds_set.end()) {
            _preds.push_back(block);
            _preds_set.insert(block);
        }
    }
    void add_succs(Block *block) {
        if (_succs_set.find(block) == _succs_set.end()) {
            _succs.push_back(block);
            _succs_set.insert(block);
        }
    }
    vector<Block *>::iterator get_preds_begin() { return _preds.begin(); }
    vector<Block *>::iterator get_preds_end() { return _preds.end(); }
    vector<Block *>::iterator get_succs_begin() { return _succs.begin(); }
    vector<Block *>::iterator get_succs_end() { return _succs.end(); }
    int get_preds_size() const { return _preds.size(); }
    int get_succs_size() const { return _succs.size(); }
    int get_id() const { return _block_id; }
    int get_begin_offset() const { return _begin_offset; }
    int get_end_offset() const { return _end_offset; }
    int get_bctsize() const { return _bctblock->get_size(); }
    int get_bct_codebegin() const { return _bct_codebegin; }
    BCTBlock *get_bctblock() const { return _bctblock; }
};
struct Excep {
    u2 from;
    u2 to;
    u2 target;
    u2 type;
    Excep(u2 a1, u2 a2, u2 a3, u2 a4)
        : from(a1), to(a2), target(a3), type(a4) {}
};
//
class BlockGraph {
  friend class BlockMatcher;
  friend class JitMatcher;
  private:
    const u1 *_code;
    u1 *_bctcode = nullptr;
    const int _code_length; // size of _code
    int _code_count = 0;    // size of _bctcode
    u4 _bc_set[7] = {0};    //
    const u1 *_exception_table;
    const u2 _exception_table_length;
    vector<Block *> _blocks;
    vector<BCTBlock *> _bctblocks;
    vector<Excep> _exceps;
    unordered_map<int, Block *> _offset2block;
    /* bct code offset to block id */
    vector<int> _block_id;
    /* bc offset to bct offset */
    unordered_map<int, int> bct_offset;
    unordered_map<int, pair<u1, u2>> _method_site;
    bool _is_build_graph;
    bool _is_build_bctlist;

  public:
    BlockGraph(const u1 *code, int code_length, const u1 *exception_table,
               const u2 exception_table_length)
        : _code_length(code_length),
          _exception_table_length(exception_table_length),
          _is_build_graph(false), _is_build_bctlist(false) {
        _code = (u1 *)malloc(code_length * sizeof(u1));
        memcpy((void *)_code, code, code_length * sizeof(u1));
        if (exception_table_length) {
            _exception_table =
                (u1 *)malloc(exception_table_length * 4 * sizeof(u2));
            memcpy((void *)_exception_table, exception_table,
                   exception_table_length * 4 * sizeof(u2));
        } else {
            _exception_table = nullptr;
        }
    };
    ~BlockGraph() {
        for (auto block : _blocks) {
            delete block;
        }
        for (auto bctblock : _bctblocks) {
            delete bctblock;
        }
        free((void *)_code);
        if (_bctcode != nullptr) {
            free((void *)_bctcode);
        }
    }
    void set_exceps(vector<Excep> exceps) {}
    vector<Excep> *get_exceps() { return &_exceps; }
    Block *offset2block(int offset);
    Block *make_block_at(int offset, Block *current);
    u4 *get_bc_set() { return _bc_set; }
    bool contain_bc_set(u4 *bc_set) {
        for (int i = 0; i < 7; ++i) {
            if (bc_set[i] != (bc_set[i] & _bc_set[i]))
                return false;
        }
        return true;
    }
    unordered_map<int, pair<u1, u2>> *get_method_site() {
        build_bctlist();
        build_graph();
        return &_method_site;
    }
    void build_graph();
    void print_graph();
    void build_bctlist();
    void print_bctlist();
};
//
class BCTBlockList {
  private:
    const u1 *_code;
    const int _code_length;
    u4 _bc_set[7] = {0}; //
    vector<BCTBlock *> _blocks;
    bool _is_build_list;
    bool _is_exception = false;

  public:
    // class file method field: code and code_length
    BCTBlockList(const u1 *code, int code_length)
        : _code(code), _code_length(code_length), _is_build_list(false){};
    ~BCTBlockList() {
        for (auto block : _blocks) {
            delete block;
        }
    }
    BCTBlock *make_block_at(const u1 *addr);
    void build_list();
    u4 *get_bc_set() { return _bc_set; }
    vector<BCTBlock *> *get_blocks() { return &_blocks; }
    bool get_exception() { return _is_exception; }
    void set_exception() { _is_exception = true; }
    void print();
};

#endif // JAVA_BLOCK_HPP
