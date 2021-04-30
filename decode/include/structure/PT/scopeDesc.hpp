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

#ifndef SHARE_VM_CODE_SCOPEDESC_HPP
#define SHARE_VM_CODE_SCOPEDESC_HPP

#include "pcDesc.hpp"

typedef unsigned char u_char;
// ScopeDescs contain the information that makes source-level debugging of
// nmethods possible; each scopeDesc describes a method activation

class ScopeDesc {
 public:
  // Constructor
  ScopeDesc(int decode_offset, int obj_decode_offset, bool reexecute, bool rethrow_exception, bool return_oop, const u_char *scopes_data);

  // JVM state
  int  method_index()     const {return _method_index; }
  int          bci()      const { return _bci;    }
  bool should_reexecute() const { return _reexecute; }
  bool rethrow_exception() const { return _rethrow_exception; }
  bool return_oop()       const { return _return_oop; }

  // Stack walking, returns NULL if this is the outer most scope.
  ScopeDesc* sender() const;

  // Returns where the scope was decoded
  int decode_offset() const { return _decode_offset; }

  int sender_decode_offset() const { return _sender_decode_offset; }

  // Tells whether sender() returns NULL
  bool is_top() const;

 private:
  void initialize(const ScopeDesc* parent, int decode_offset);
  // Alternative constructors
  ScopeDesc(const ScopeDesc* parent);

  // JVM state
  int           _method_index;
  int           _bci;
  bool          _reexecute;
  bool          _rethrow_exception;
  bool          _return_oop;

  const u_char *_scopes_data;

  // Decoding offsets
  int _decode_offset;
  int _sender_decode_offset;
  int _locals_decode_offset;
  int _expressions_decode_offset;
  int _monitors_decode_offset;

  int _obj_decode_offset;

  // Decoding operations
  void decode_body();

};

#endif // SHARE_VM_CODE_SCOPEDESC_HPP
