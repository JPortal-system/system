#ifndef JAVA_BUFFER_STREAM_HPP
#define JAVA_BUFFER_STREAM_HPP
#include "type_defs.hpp"

#include <cassert>

// Read from Big-Endian stream on Little-Endian platform like x86
class BufferStream {
  private:
    const u1 *const _buffer_begin;
    const u1 *const _buffer_end;
    mutable const u1 *_current;

  public:
    BufferStream(const u1 *buffer, int length)
        : _buffer_begin(buffer), _buffer_end(buffer + length),
          _current(buffer) {}
    ~BufferStream() {}

    const u1 *current() const { return _current; }
    void set_current() const { _current = _buffer_begin; }
    void set_current(const u1 *pos) const {
        assert(pos >= _buffer_begin && pos <= _buffer_end);
        _current = pos;
    }
    int get_offset() const { return _current - _buffer_begin; }
    // Read u1 from stream;
    u1 get_u1() const { return *_current++; }

    // Read u2 from Big-Endian stream;
    u2 get_u2() const {
        u1 us[2];
        us[1] = *_current++;
        us[0] = *_current++;
        return *(u2 *)us;
    }

    // Read u2 from Little-Endian stream;
    u2 get_u2_l() const {
        u1 us[2];
        us[0] = *_current++;
        us[1] = *_current++;
        return *(u2 *)us;
    }

    // Read u4 from Big-Endian stream;
    u4 get_u4() const {
        u1 ui[4];
        ui[3] = *_current++;
        ui[2] = *_current++;
        ui[1] = *_current++;
        ui[0] = *_current++;
        return *(u4 *)ui;
    }

    // Read u8 from Big-Endian stream;
    u8 get_u8() const {
        u1 ul[8];
        ul[7] = *_current++;
        ul[6] = *_current++;
        ul[5] = *_current++;
        ul[4] = *_current++;
        ul[3] = *_current++;
        ul[2] = *_current++;
        ul[1] = *_current++;
        ul[0] = *_current++;
        return *(u8 *)ul;
    }
    // Skip length u1 or u2 elements from stream
    void skip_u1(int length) const { _current += length; }

    void skip_u2(int length) const { _current += 2 * length; }

    void skip_u4(int length) const { _current += 4 * length; }

    // Tells whether eos is reached
    bool at_eos() const { return _current == _buffer_end; }
};
#endif // JAVA_BUFFER_STREAM_HPP