#ifndef JAVA_CLASSFILEPARSER_HPP
#define JAVA_CLASSFILEPARSER_HPP

#include "structure/common.hpp"
#include "buffer_stream.hpp"
#include "constantPool.hpp"
#include "klass.hpp"
#include "method.hpp"

class ClassFileParser {
  private:
    BufferStream *_stream = nullptr;
    u1 *_buffer = nullptr;
    Klass &_klass;
    // string _class_name;
    // int _num_patched_klasses;
    // int _max_num_patched_klasses;
    // int _first_patched_klass_resolved_index;

    u2 _major_version;
    u2 _minor_version;
    // AccessFlags _access_flags;
    u2 _this_class_index;
    u2 _super_class_index;
    ConstantPool *_cp = nullptr;
    // vector<u2> _interfaces_index;

    // int _orig_cp_size;

    // u2 _itfs_len;
    // u2 _java_fields_count;

    // bool _has_nonstatic_concrete_methods;
    // bool _declares_nonstatic_concrete_methods;
    // bool _has_final_method;

    // Constant pool parsing
    void parse_constant_pool(const BufferStream *const stream,
                             ConstantPool *const cp, const int length);

    // Field parsing
    void parse_fields(const BufferStream *const stream, ConstantPool *cp);

    // Method parsing
    void parse_methods(const BufferStream *const stream, ConstantPool *cp);

    Method *parse_method(const BufferStream *const stream, ConstantPool *cp);

    // Classfile attribute parsing
    void parse_classfile_attributes(const BufferStream *const stream);

  public:
    ClassFileParser(string file_path, Klass &klass);
    ~ClassFileParser();

    void parse_class();
};

#endif // JAVA_CLASSFILEPARSER_HPP