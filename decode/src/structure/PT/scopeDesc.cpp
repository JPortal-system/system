/*
 * Copyright (c) 1997, 2016, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "structure/PT/pcDesc.hpp"
#include "structure/PT/scopeDesc.hpp"
#include "structure/PT/compressedStream.hpp"

const int serialized_null = 0;
const int InvocationEntryBci   = -1;

ScopeDesc::ScopeDesc(int decode_offset, int obj_decode_offset, bool reexecute, bool rethrow_exception, bool return_oop, const u_char *scopes_data) {
  _decode_offset = decode_offset;
  _obj_decode_offset = obj_decode_offset;
  _reexecute     = reexecute;
  _rethrow_exception = rethrow_exception;
  _return_oop    = return_oop;
  _scopes_data = scopes_data;
  decode_body();
}

void ScopeDesc::initialize(const ScopeDesc* parent, int decode_offset) {
  _decode_offset = decode_offset;
  _obj_decode_offset = parent->_obj_decode_offset;
  _reexecute     = false; //reexecute only applies to the first scope
  _rethrow_exception = false;
  _return_oop    = false;
  _scopes_data = parent->_scopes_data;
  decode_body();
}

ScopeDesc::ScopeDesc(const ScopeDesc* parent) {
  initialize(parent, parent->_sender_decode_offset);
}

void ScopeDesc::decode_body() {
  if (decode_offset() == serialized_null) {
    // This is a sentinel record, which is only relevant to
    // approximate queries.  Decode a reasonable frame.
    _sender_decode_offset = serialized_null;
    _method_index = 1; /* should be code->method */
    _bci = InvocationEntryBci;
    _locals_decode_offset = serialized_null;
    _expressions_decode_offset = serialized_null;
    _monitors_decode_offset = serialized_null;
  } else {
    // decode header
    CompressedReadStream stream(_scopes_data, _decode_offset);

    _sender_decode_offset = stream.read_int();
    _method_index = stream.read_int();
    _bci    = stream.read_int() + InvocationEntryBci;

    _locals_decode_offset = stream.read_int();
    _expressions_decode_offset = stream.read_int();
    _monitors_decode_offset = stream.read_int();
  }
}

bool ScopeDesc::is_top() const {
 return _sender_decode_offset == serialized_null;
}

ScopeDesc* ScopeDesc::sender() const {
  if (is_top()) return nullptr;
  return new ScopeDesc(this);
}