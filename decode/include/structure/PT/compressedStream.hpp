/*
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_CODE_COMPRESSEDSTREAM_HPP
#define SHARE_VM_CODE_COMPRESSEDSTREAM_HPP

#include <stdint.h>

typedef unsigned char u_char;

typedef uint32_t juint;
typedef int jint;
typedef unsigned char   jboolean;
typedef unsigned short  jchar;
typedef short           jshort;
typedef float           jfloat;
typedef double          jdouble;
typedef signed char jbyte;

typedef jint            jsize;

const int LogBitsPerByte     = 3;
const int BitsPerByte        = 1 << LogBitsPerByte; 
// Simple interface for filing out and filing in basic types
// Used for writing out and reading in debugging information.

class CompressedStream{
 protected:
  const u_char* _buffer;
  int     _position;

  enum {
    // Constants for UNSIGNED5 coding of Pack200
    lg_H = 6, H = 1<<lg_H,    // number of high codes (64)
    L = (1<<BitsPerByte)-H,   // number of low codes (192)
    MAX_i = 4                 // bytes are numbered in (0..4), max 5 bytes
  };

  // these inlines are defined only in compressedStream.cpp
  static inline juint encode_sign(jint  value){return (value << 1) ^ (value >> 31);}  // for Pack200 SIGNED5
  static inline jint  decode_sign(juint value){return (value >> 1) ^ -(jint)(value & 1);}  // for Pack200 SIGNED5

 public:
  CompressedStream(const u_char* buffer, int position = 0) {
    _buffer   = buffer;
    _position = position;
  }

  const u_char* buffer() const               { return _buffer; }

  // Positioning
  int position() const                 { return _position; }
  void set_position(int position)      { _position = position; }
};


class CompressedReadStream : public CompressedStream {
 private:
  inline u_char read()                 { return _buffer[_position++]; }

  // This encoding, called UNSIGNED5, is taken from J2SE Pack200.
  // It assumes that most values have lots of leading zeroes.
  // Very small values, in the range [0..191], code in one byte.
  // Any 32-bit value (including negatives) can be coded, in
  // up to five bytes.  The grammar is:
  //    low_byte  = [0..191]
  //    high_byte = [192..255]
  //    any_byte  = low_byte | high_byte
  //    coding = low_byte
  //           | high_byte low_byte
  //           | high_byte high_byte low_byte
  //           | high_byte high_byte high_byte low_byte
  //           | high_byte high_byte high_byte high_byte any_byte
  // Each high_byte contributes six bits of payload.
  // The encoding is one-to-one (except for integer overflow)
  // and easy to parse and unparse.

  jint read_int_mb(jint b0) {
    int     pos = position() - 1;
    const u_char* buf = buffer() + pos;
    jint    sum = b0;
    // must collect more bytes:  b[1]...b[4]
    int lg_H_i = lg_H;
    for (int i = 0; ; ) {
      jint b_i = buf[++i]; // b_i = read(); ++i;
      sum += b_i << lg_H_i;  // sum += b[i]*(64**i)
      if (b_i < L || i == MAX_i) {
        set_position(pos+i+1);
        return sum;
      }
      lg_H_i += lg_H;
    }
  }

 public:
  CompressedReadStream(const u_char* buffer, int position = 0)
  : CompressedStream(buffer, position) {}

  jboolean read_bool()                 { return (jboolean) read();      }
  jbyte    read_byte()                 { return (jbyte   ) read();      }
  jchar    read_char()                 { return (jchar   ) read_int();  }
  jshort   read_short()                { return (jshort  ) read_signed_int(); }
  jint     read_int()                  { jint   b0 = read();
                                         if (b0 < L)  return b0;
                                         else         return read_int_mb(b0);
                                       }
  jint     read_signed_int()           {return decode_sign(read_int()); }
};

#endif