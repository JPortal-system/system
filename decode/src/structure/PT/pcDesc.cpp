#include "structure/PT/pcDesc.hpp"

PcDesc::PcDesc(int pc_offset, int scope_decode_offset, int obj_decode_offset) {
  _pc_offset           = pc_offset;
  _scope_decode_offset = scope_decode_offset;
  _obj_decode_offset   = obj_decode_offset;
  _flags               = 0;
}

uint64_t PcDesc::real_pc(uint64_t code_begin) const {
  return code_begin + pc_offset();
}

bool PcDesc::verify() {
  //Unimplemented();
  return true;
}
