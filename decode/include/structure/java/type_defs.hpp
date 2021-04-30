#ifndef JAVA_TYPE_DEFS_HPP
#define JAVA_TYPE_DEFS_HPP

using u_char = unsigned char;
using u_int = unsigned int;

// Java type definitions
using jint = int;
using jlong = long;

// Unsigned byte types for parsing *.class
using u1 = unsigned char;
using u2 = unsigned short;
using u4 = unsigned int;
using u8 = unsigned long long;

inline jint alignup(jint offset) {
    jint alignup = offset % 4;
    if (alignup)
        return (4 - alignup);
    return 0;
}

#endif // JAVA_TYPE_DEFS_HPP