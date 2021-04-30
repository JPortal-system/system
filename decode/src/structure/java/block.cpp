#include "structure/java/block.hpp"
#include "structure/java/buffer_stream.hpp"
#include "structure/java/bytecodes.hpp"
#include "structure/common.hpp"

#include <bitset>
#include <iomanip>
#include <iostream>
using namespace std;

Block *BlockGraph::offset2block(int offset) {
    auto iter = _offset2block.find(offset);
    if (iter != _offset2block.end()) {
        return iter->second;
    } else {
        return nullptr;
    }
}

Block *BlockGraph::make_block_at(int offset, Block *current) {
    // cout << "make_block_at" << offset << endl;
    Block *block = offset2block(offset);
    if (block == nullptr) {
        int id = _blocks.size();
        _blocks.push_back(new Block(id, offset, bct_offset[offset]));
        block = _blocks[id];
        _offset2block[offset] = block;
    }
    if (current != nullptr) {
        current->add_succs(block);
        block->add_preds(current);
    }
    return block;
}

void BlockGraph::build_graph() {
    if (_is_build_graph)
        return;
    int current_offset = 0;
    Block *current = nullptr;
    BufferStream bs(_code, _code_length);
    int bc_size = 0;
    _code_count = 0;
    unordered_set<int> block_start;
    block_start.insert(0);
    unordered_set<int> jsr_following;
    while (!bs.at_eos()) {
        bct_offset[bs.get_offset()] = _code_count;
        // cout << "build_graph: " << bs.get_offset() << endl;
        Bytecodes::Code bc = Bytecodes::cast(bs.get_u1());
        ++_code_count;
        // _bc_set;
        int index = bc >> 5;
        int ls = bc & 31;
        // cout << _code_count << ":" << bc << ":" << index << ":" << ls <<
        // endl;
        _bc_set[index] = _bc_set[index] | (1 << ls);
        if (Bytecodes::is_block_end(bc)) {
            switch (bc) {
            // track bytecodes that affect the control flow
            case Bytecodes::_athrow:  // fall through
                block_start.insert(bs.get_offset());
                break;
            case Bytecodes::_ret:     // fall through
                bs.get_u1();
                block_start.insert(bs.get_offset());
                break;
            case Bytecodes::_ireturn: // fall through
            case Bytecodes::_lreturn: // fall through
            case Bytecodes::_freturn: // fall through
            case Bytecodes::_dreturn: // fall through
            case Bytecodes::_areturn: // fall through
            case Bytecodes::_return:
                block_start.insert(bs.get_offset());
                break;

            case Bytecodes::_ifeq:      // fall through
            case Bytecodes::_ifne:      // fall through
            case Bytecodes::_iflt:      // fall through
            case Bytecodes::_ifge:      // fall through
            case Bytecodes::_ifgt:      // fall through
            case Bytecodes::_ifle:      // fall through
            case Bytecodes::_if_icmpeq: // fall through
            case Bytecodes::_if_icmpne: // fall through
            case Bytecodes::_if_icmplt: // fall through
            case Bytecodes::_if_icmpge: // fall through
            case Bytecodes::_if_icmpgt: // fall through
            case Bytecodes::_if_icmple: // fall through
            case Bytecodes::_if_acmpeq: // fall through
            case Bytecodes::_if_acmpne: // fall through
            case Bytecodes::_ifnull:    // fall through
            case Bytecodes::_ifnonnull: {
                int jmp_offset = (short)bs.get_u2();
                current_offset = bs.get_offset();
                block_start.insert(current_offset);
                block_start.insert(current_offset + jmp_offset - 3);
                break;
            }
            case Bytecodes::_goto: {
                int jmp_offset = (short)bs.get_u2();
                current_offset = bs.get_offset();
                block_start.insert(current_offset + jmp_offset - 3);
                break;
            }
            case Bytecodes::_goto_w: {
                int jmp_offset = bs.get_u4();
                current_offset = bs.get_offset();
                block_start.insert(current_offset + jmp_offset - 5);
                break;
            }
            case Bytecodes::_jsr: {
                int jmp_offset = (short)bs.get_u2();
                current_offset = bs.get_offset();
                block_start.insert(current_offset + jmp_offset - 3);
                jsr_following.insert(current_offset);
                break;
            }
            case Bytecodes::_jsr_w: {
                int jmp_offset = bs.get_u4();
                current_offset = bs.get_offset();
                block_start.insert(current_offset + jmp_offset - 5);
                jsr_following.insert(current_offset);
                break;
            }
            case Bytecodes::_tableswitch: {
                jint offset = bs.get_offset();
                int bc_len =
                    Bytecodes::special_length_at(bc, bs.current() - 1, offset);
                bs.skip_u1(bc_len - 1);
                break;
            }

            case Bytecodes::_lookupswitch: {
                jint offset = bs.get_offset();
                int bc_len =
                    Bytecodes::special_length_at(bc, bs.current() - 1, offset);
                bs.skip_u1(bc_len - 1);
                break;
            }

            default: {
                bc_size = Bytecodes::length_for(bc);
                if (bc_size == 0) {
                    jint offset = bs.get_offset();
                    bc_size = Bytecodes::special_length_at(bc, bs.current() - 1,
                                                           offset);
                }
                bs.skip_u1(bc_size - 1);
                break;
            }
            } // End of Switch
        } else if (bc == Bytecodes::_invokevirtual ||
                    bc == Bytecodes::_invokespecial ||
                    bc == Bytecodes::_invokestatic) {
            u2 methodref_index = bs.get_u2();
            _method_site[_code_count] = make_pair(bc, methodref_index);
            block_start.insert(bs.get_offset());
        } else if (bc == Bytecodes::_invokeinterface) {
            u2 interface_methodref_index = bs.get_u2();
            _method_site[_code_count] = make_pair(bc, interface_methodref_index);
            bs.get_u2();
            block_start.insert(bs.get_offset());
        } else if (bc == Bytecodes::_invokedynamic) {
            u2 dynamic_method_index = bs.get_u2();
            _method_site[_code_count] = make_pair(bc, dynamic_method_index);
            bs.get_u2();
            block_start.insert(bs.get_offset());
        } else {
            bc_size = Bytecodes::length_for(bc);
            if (bc_size == 0) {
                jint offset = bs.get_offset();
                bc_size =
                    Bytecodes::special_length_at(bc, bs.current() - 1, offset);
            }
            bs.skip_u1(bc_size - 1);
        }
    }
    // cout << "block_start" << endl;
    // for (auto blockst : block_start) {
    //     cout << blockst << endl;
    // }
    _code_count = 0;
    bs.set_current();
    while (!bs.at_eos()) {
        // cout << "build_graph: " << bs.get_offset() << endl;
        if (current == nullptr) {
            current_offset = bs.get_offset();
            current = make_block_at(current_offset, current);
            _block_id.push_back(current->get_id());
        } else {
            current_offset = bs.get_offset();
            auto exist = block_start.find(current_offset);
            if (exist != block_start.end()) {
                Block *temp = make_block_at(current_offset, current);
                current->set_end_offset(current_offset);
                current = temp;
            }
            _block_id.push_back(current->get_id());
        }
        Bytecodes::Code bc = Bytecodes::cast(bs.get_u1());
        ++_code_count;
        if (Bytecodes::is_block_end(bc)) {
            switch (bc) {
            // track bytecodes that affect the control flow
            case Bytecodes::_ret:     // fall through
                bs.get_u1();
                current_offset = bs.get_offset();
                for (auto jsr_off : jsr_following) {
                    make_block_at(jsr_off, current);
                }
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            case Bytecodes::_athrow:  // fall through
            case Bytecodes::_ireturn: // fall through
            case Bytecodes::_lreturn: // fall through
            case Bytecodes::_freturn: // fall through
            case Bytecodes::_dreturn: // fall through
            case Bytecodes::_areturn: // fall through
            case Bytecodes::_return:
                current_offset = bs.get_offset();
                current->set_end_offset(current_offset);
                current = nullptr;
                break;

            case Bytecodes::_ifeq:      // fall through
            case Bytecodes::_ifne:      // fall through
            case Bytecodes::_iflt:      // fall through
            case Bytecodes::_ifge:      // fall through
            case Bytecodes::_ifgt:      // fall through
            case Bytecodes::_ifle:      // fall through
            case Bytecodes::_if_icmpeq: // fall through
            case Bytecodes::_if_icmpne: // fall through
            case Bytecodes::_if_icmplt: // fall through
            case Bytecodes::_if_icmpge: // fall through
            case Bytecodes::_if_icmpgt: // fall through
            case Bytecodes::_if_icmple: // fall through
            case Bytecodes::_if_acmpeq: // fall through
            case Bytecodes::_if_acmpne: // fall through
            case Bytecodes::_ifnull:    // fall through
            case Bytecodes::_ifnonnull: {
                int jmp_offset = (short)bs.get_u2();
                current_offset = bs.get_offset();
                make_block_at(current_offset, current);
                make_block_at(current_offset + jmp_offset - 3, current);
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            }
            case Bytecodes::_goto: {
                int jmp_offset = (short)bs.get_u2();
                current_offset = bs.get_offset();
                make_block_at(current_offset + jmp_offset - 3, current);
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            }
            case Bytecodes::_goto_w: {
                int jmp_offset = bs.get_u4();
                current_offset = bs.get_offset();
                make_block_at(current_offset + jmp_offset - 5, current);
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            }
            case Bytecodes::_jsr: {
                int jmp_offset = (short)bs.get_u2();
                current_offset = bs.get_offset();
                make_block_at(current_offset + jmp_offset - 3, current);
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            }
            case Bytecodes::_jsr_w: {
                int jmp_offset = bs.get_u4();
                current_offset = bs.get_offset();
                make_block_at(current_offset + jmp_offset - 5, current);
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            }
            case Bytecodes::_tableswitch: {
                current_offset = bs.get_offset();
                int bc_addr = current_offset - 1;
                bs.skip_u1(alignup(current_offset));
                int default_offset = bs.get_u4();
                // default
                make_block_at(bc_addr + default_offset, current);
                jlong lo = bs.get_u4();
                jlong hi = bs.get_u4();
                int case_count = hi - lo + 1;
                for (int i = 0; i < case_count; i++) {
                    int case_offset = bs.get_u4();
                    // case
                    make_block_at(bc_addr + case_offset, current);
                }
                current_offset = bs.get_offset();
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            }

            case Bytecodes::_lookupswitch: {
                current_offset = bs.get_offset();
                int bc_addr = current_offset - 1;
                bs.skip_u1(alignup(current_offset));
                int default_offset = bs.get_u4();
                // default
                make_block_at(bc_addr + default_offset, current);
                jlong npairs = bs.get_u4();
                for (int i = 0; i < npairs; i++) {
                    int case_key = bs.get_u4();
                    int case_offset = bs.get_u4();
                    // case
                    make_block_at(bc_addr + case_offset, current);
                }
                current_offset = bs.get_offset();
                current->set_end_offset(current_offset);
                current = nullptr;
                break;
            }

            default:
                break;
            }
        } else if (bc == Bytecodes::_invokevirtual ||
                    bc == Bytecodes::_invokespecial ||
                    bc == Bytecodes::_invokestatic) {
            bs.get_u2();
            make_block_at(bs.get_offset(), current);
            current->set_end_offset(bs.get_offset());
            current = nullptr;
        } else if (bc == Bytecodes::_invokeinterface) {
            bs.get_u2();
            bs.get_u2();
            make_block_at(bs.get_offset(), current);
            current->set_end_offset(bs.get_offset());
            current = nullptr;
        } else if (bc == Bytecodes::_invokedynamic) {
            bs.get_u2();
            bs.get_u2();
            make_block_at(bs.get_offset(), current);
            current->set_end_offset(bs.get_offset());
            current = nullptr;
        } else {
            bc_size = Bytecodes::length_for(bc);
            if (bc_size == 0) {
                jint offset = bs.get_offset();
                bc_size =
                    Bytecodes::special_length_at(bc, bs.current() - 1, offset);
            }
            bs.skip_u1(bc_size - 1);
        }
    }
    // build exception table
    BufferStream excep_bs(_exception_table,
                          _exception_table_length * 4 * sizeof(u2));
    for (int i = 0; i < _exception_table_length; ++i) {
        u2 a1 = excep_bs.get_u2();
        u2 a2 = excep_bs.get_u2();
        u2 a3 = excep_bs.get_u2();
        u2 a4 = excep_bs.get_u2();
        Excep ecp(a1, a2, a3, a4);
        _exceps.push_back(ecp);
    }
    //
    _is_build_graph = true;
}

void BlockGraph::print_graph() {
    if (!_is_build_graph)
        build_graph();

    for (auto block : _blocks) {
        // Id
        cout << "[B" << block->get_id() << "]: [" << block->get_begin_offset()
             << ", " << block->get_end_offset() << ")" << endl;
        // Preds
        int preds_size = block->get_preds_size();
        int succs_size = block->get_succs_size();
        if (preds_size) {
            cout << " Preds (" << preds_size << "):";
            auto iter = block->get_preds_begin();
            auto end = block->get_preds_end();
            while (iter != end) {
                cout << " B" << (*iter)->get_id();
                ++iter;
            }
            cout << endl;
        }
        // Succs
        if (succs_size) {
            cout << " Succs (" << succs_size << "):";
            auto iter = block->get_succs_begin();
            auto end = block->get_succs_end();
            while (iter != end) {
                cout << " B" << (*iter)->get_id();
                ++iter;
            }
            cout << endl;
        }
    }
    if (_exception_table_length) {
        cout << "Exception table:" << endl;
        cout << "\tfrom\tto\ttarget" << endl;
    }
    for (auto ecp : _exceps) {
        cout << "\t" << ecp.from << "\t" << ecp.to << "\t" << ecp.target
             << endl;
    }
    if (_method_site.size()) {
        cout << "call_site:" << endl;
        for (auto call : _method_site) {
            cout << setw(6) << call.first << ": #" << call.second.second << endl;
        }
    }

    cout << "bc_set:" << endl;
    int start = 0;
    for (int i = 0; i < 7; ++i) {
        cout << setw(3) << start + 31 << "~" << setw(3) << start << ": "
             << bitset<sizeof(u4) * 8>(_bc_set[i]) << endl;
        start += 32;
    }
}

void BlockGraph::build_bctlist() {
    if (_is_build_bctlist)
        return;
    if (!_is_build_graph) {
        build_graph();
    }
    _bctcode = (u1 *)malloc(_code_count * sizeof(u1));
    _bctblocks.resize(_blocks.size(), nullptr);
    u1 *_bytes_index = _bctcode;
    BCTBlock *bctbk;
    BCTBlock *pred_bctbk = nullptr;
    Block *bk;
    Block *current_bk;
    for (u4 i = 0; i < _code_length;) {
        bk = offset2block(i);
        if (bk != nullptr) {
            current_bk = bk;
            if (pred_bctbk != nullptr)
                pred_bctbk->set_end(_bytes_index);
            int bctid = bk->get_id();
            bctbk = new BCTBlock(bctid, _bytes_index);
            pred_bctbk = bctbk;
            bk->set_bctblock(bctbk);
            _bctblocks[bctid] = bctbk;
        }
        u1 ins = *(_code + i);
        *_bytes_index = ins;
        ++_bytes_index;
        int len = Bytecodes::length_for((Bytecodes::Code)ins);
        if (len == 0) {
            len = Bytecodes::special_length_at((Bytecodes::Code)ins, _code + i,
                                               i + 1);
        }
        i += len;
    }
    if (pred_bctbk != nullptr)
        pred_bctbk->set_end(_bytes_index);
    _is_build_bctlist = true;
}

void BlockGraph::print_bctlist() {
    if (!_is_build_bctlist)
        build_bctlist();
    for (auto block : _bctblocks) {
        // Id
        cout << "[B" << block->get_id() << "]: ["
             << block->get_begin() - _bctcode << ", "
             << block->get_end() - _bctcode << ")" << endl;
        // Bytecodes
        for (const u1 *ttt = block->get_begin(); ttt != block->get_end();
             ttt++) {
            cout << Bytecodes::name_for(Bytecodes::Code(*ttt)) << " ";
        }
        cout << endl;
    }
}

BCTBlock *BCTBlockList::make_block_at(const u1 *addr) {
    int id = _blocks.size();
    _blocks.push_back(new BCTBlock(id, addr));
    return _blocks[id];
}

void BCTBlockList::build_list() {
    if (_is_build_list)
        return;
    BCTBlock *current = nullptr;
    BufferStream bs(_code, _code_length);
    int bc_size = 0;
    if (!bs.at_eos()) {
        // exception handling indication
        Bytecodes::Code bc = Bytecodes::cast(*_code);
        if (!Bytecodes::is_valid(bc)) {
            _is_exception = true;
            bs.get_u1();
        }
    }
    while (!bs.at_eos()) {
        if (current == nullptr)
            current = make_block_at(bs.current());
        Bytecodes::Code bc = Bytecodes::cast(bs.get_u1());

        // _bc_set;
        int index = bc >> 5;
        int ls = bc & 31;
        _bc_set[index] = _bc_set[index] | (1 << ls);
        /* not end with a return, end of a section */
        if (bs.at_eos() && !Bytecodes::is_return(bc) && bc != Bytecodes::_athrow)
            current->set_branch(-1);
        if (Bytecodes::is_block_end(bc)) {
            switch (bc) {
            // track bytecodes that affect the control flow
            case Bytecodes::_athrow:  // fall through
            case Bytecodes::_ret:     // fall through
            case Bytecodes::_ireturn: // fall through
            case Bytecodes::_lreturn: // fall through
            case Bytecodes::_freturn: // fall through
            case Bytecodes::_dreturn: // fall through
            case Bytecodes::_areturn: // fall through
            case Bytecodes::_return:
                current->set_end(bs.current());
                current = nullptr;
                break;

            case Bytecodes::_ifeq:      // fall through
            case Bytecodes::_ifne:      // fall through
            case Bytecodes::_iflt:      // fall through
            case Bytecodes::_ifge:      // fall through
            case Bytecodes::_ifgt:      // fall through
            case Bytecodes::_ifle:      // fall through
            case Bytecodes::_if_icmpeq: // fall through
            case Bytecodes::_if_icmpne: // fall through
            case Bytecodes::_if_icmplt: // fall through
            case Bytecodes::_if_icmpge: // fall through
            case Bytecodes::_if_icmpgt: // fall through
            case Bytecodes::_if_icmple: // fall through
            case Bytecodes::_if_acmpeq: // fall through
            case Bytecodes::_if_acmpne: // fall through
            case Bytecodes::_ifnull:    // fall through
            case Bytecodes::_ifnonnull:
                current->set_end(bs.current());
                if (!bs.at_eos()) {
                    u1 branch = bs.get_u1();
                    current->set_branch(branch);
                }
                current = nullptr;
                break;

            case Bytecodes::_goto:
                current->set_end(bs.current());
                current = nullptr;
                break;

            case Bytecodes::_goto_w:
                current->set_end(bs.current());
                current = nullptr;
                break;

            case Bytecodes::_jsr:
                // TODO_JK
                current->set_end(bs.current());
                current = nullptr;
                break;

            case Bytecodes::_jsr_w:
                // TODO_JK
                current->set_end(bs.current());
                current = nullptr;
                break;

            case Bytecodes::_tableswitch: {
                // next block is which case
                // TODO_JK
                current->set_end(bs.current());
                current->set_branch(2);
                current = nullptr;
                break;
            }

            case Bytecodes::_lookupswitch: {
                // next block is which case
                // TODO_JK
                current->set_end(bs.current());
                current->set_branch(2);
                current = nullptr;
                break;
            }

            default:
                break;
            }
        }
    }
    if (current)
        current->set_end(bs.current());
    _is_build_list = true;
}

void BCTBlockList::print() {
    if (!_is_build_list)
        build_list();

    /* print */
    for (auto block : _blocks) {
        // Id
        cout << "[B" << block->get_id() << "]";
        // Bytecodes
        const u1 *begin = block->get_begin();
        const u1 *end = block->get_end();
        cout << ": " << begin - _code;
        cout << ": " << end - _code << endl;
        cout << "    ";
        const u1 *buffer;
        for (buffer = begin; buffer < end; buffer++) {
            cout << Bytecodes::name_for((Bytecodes::Code)*buffer) << " ";
        }
        cout << "branch " << block->get_branch() << endl;
    }
}