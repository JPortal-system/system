#ifndef JAVA_CONSTANTPOOL_HPP
#define JAVA_CONSTANTPOOL_HPP

#include "structure/common.hpp"
#include "type_defs.hpp"

// Constant Pool Tag
typedef enum {
    CONSTANT_Utf8_info = 1,
    CONSTANT_Unicode_info = 2, /* unused */
    CONSTANT_Integer_info = 3,
    CONSTANT_Float_info = 4,
    CONSTANT_Long_info = 5,
    CONSTANT_Double_info = 6,
    CONSTANT_Class_info = 7,
    CONSTANT_String_info = 8,
    CONSTANT_Fieldref_info = 9,
    CONSTANT_Methodref_info = 10,
    CONSTANT_InterfaceMethodref_info = 11,
    CONSTANT_NameAndType_info = 12,
    CONSTANT_MethodHandle_info = 15, // JSR 292
    CONSTANT_MethodType_info = 16,   // JSR 292
    CONSTANT_Dynamic_info = 17,
    CONSTANT_InvokeDynamic_info = 18,
    CONSTANT_ExternalMax_info = 18

} Tag;

class Constant {
  private:
  protected:
    const Tag _tag;

  public:
    Constant(Tag tag) : _tag(tag){};
    Tag tag() { return _tag; }
};
// 1 = CONSTANT_Utf8_info
class Constant_Utf8_info : public Constant {
  private:
    const u2 _length;
    const string _str;

  public:
    Constant_Utf8_info(Tag tag, u2 length, string str)
        : Constant(tag), _length(length), _str(str) {
        assert(_tag == CONSTANT_Utf8_info);
    };

    string str() { return _str; }
};
// 3 = CONSTANT_Integer_info
class Constant_Integer_info : public Constant {
  private:
    const u4 _bytes;

  public:
    Constant_Integer_info(Tag tag, u4 bytes) : Constant(tag), _bytes(bytes) {
        assert(_tag == CONSTANT_Integer_info);
    };
};
// 4 = CONSTANT_Float_info
class Constant_Float_info : public Constant {
  private:
    const u4 _bytes;

  public:
    Constant_Float_info(Tag tag, u4 bytes) : Constant(tag), _bytes(bytes) {
        assert(_tag == CONSTANT_Float_info);
    };
};
// 5 = CONSTANT_Long_info
class Constant_Long_info : public Constant {
  private:
    const u8 _bytes;

  public:
    Constant_Long_info(Tag tag, u8 bytes) : Constant(tag), _bytes(bytes) {
        assert(_tag == CONSTANT_Long_info);
    };
};
// 6 = CONSTANT_Double_info
class Constant_Double_info : public Constant {
  private:
    const u8 _bytes;

  public:
    Constant_Double_info(Tag tag, u8 bytes) : Constant(tag), _bytes(bytes) {
        assert(_tag == CONSTANT_Double_info);
    };
};
// 7 = CONSTANT_Class_info
class Constant_Class_info : public Constant {
  private:
    const u2 _name_index;

  public:
    Constant_Class_info(Tag tag, u2 name_index)
        : Constant(tag), _name_index(name_index) {
        assert(_tag == CONSTANT_Class_info);
    };
    u2 get_name_index() { return _name_index; }
};
// 8 = CONSTANT_String_info
class Constant_String_info : public Constant {
  private:
    const u2 _string_index;

  public:
    Constant_String_info(Tag tag, u2 string_index)
        : Constant(tag), _string_index(string_index) {
        assert(_tag == CONSTANT_String_info);
    };
};
// 9 = CONSTANT_Fieldref_info
class Constant_Fieldref_info : public Constant {
  private:
    const u2 _class_index;
    const u2 _name_and_type_index;

  public:
    Constant_Fieldref_info(Tag tag, u2 class_index, u2 name_and_type_index)
        : Constant(tag), _class_index(class_index),
          _name_and_type_index(class_index) {
        assert(_tag == CONSTANT_Fieldref_info);
    };
};
// 10 = CONSTANT_Methodref_info
class Constant_Methodref_info : public Constant {
  private:
    const u2 _class_index;
    const u2 _name_and_type_index;

  public:
    Constant_Methodref_info(Tag tag, u2 class_index, u2 name_and_type_index)
        : Constant(tag), _class_index(class_index),
          _name_and_type_index(name_and_type_index) {
        assert(_tag == CONSTANT_Methodref_info);
    };
    u2 get_class_index() { return _class_index; }
    u2 get_name_and_type_index() { return _name_and_type_index; }
};
// 11 = CONSTANT_InterfaceMethodref_info
class Constant_InterfaceMethodref_info : public Constant {
  private:
    const u2 _class_index;
    const u2 _name_and_type_index;

  public:
    Constant_InterfaceMethodref_info(Tag tag, u2 class_index,
                                     u2 name_and_type_index)
        : Constant(tag), _class_index(class_index),
          _name_and_type_index(name_and_type_index) {
        assert(_tag == CONSTANT_InterfaceMethodref_info);
    };
    u2 get_class_index() { return _class_index; }
    u2 get_name_and_type_index() { return _name_and_type_index; }
};
// 12 = CONSTANT_NameAndType_info
class Constant_NameAndType_info : public Constant {
  private:
    const u2 _name_index;
    const u2 _signature_index;

  public:
    Constant_NameAndType_info(Tag tag, u2 name_index, u2 signature_index)
        : Constant(tag), _name_index(name_index),
          _signature_index(signature_index) {
        assert(_tag == CONSTANT_NameAndType_info);
    };
    u2 get_name_index() { return _name_index; }
    u2 get_type_index() { return _signature_index; }
};
// 15 = CONSTANT_MethodHandle_info
class Constant_MethodHandle_info : public Constant {
  private:
    const u1 _ref_kind;
    const u2 _method_index;

  public:
    Constant_MethodHandle_info(Tag tag, u2 ref_kind, u2 method_index)
        : Constant(tag), _ref_kind(ref_kind), _method_index(method_index) {
        assert(_tag == CONSTANT_MethodHandle_info);
    };
};
// 16 = CONSTANT_MethodType_info
class Constant_MethodType_info : public Constant {
  private:
    const u2 _signature_index;

  public:
    Constant_MethodType_info(Tag tag, u2 signature_index)
        : Constant(tag), _signature_index(signature_index) {
        assert(_tag == CONSTANT_MethodType_info);
    };
};
// 17 = CONSTANT_Dynamic_info
class Constant_Dynamic_info : public Constant {
  private:
    const u2 _bootstrap_specifier_index;
    const u2 _name_and_type_index;

  public:
    Constant_Dynamic_info(Tag tag, u2 bootstrap_specifier_index,
                          u2 name_and_type_index)
        : Constant(tag), _bootstrap_specifier_index(bootstrap_specifier_index),
          _name_and_type_index(name_and_type_index) {
        assert(_tag == CONSTANT_Dynamic_info);
    }
    u2 get_name_and_type_index() { return _name_and_type_index; }
};
// 18 = CONSTANT_InvokeDynamic_info
class Constant_InvokeDynamic_info : public Constant {
  private:
    const u2 _bootstrap_specifier_index;
    const u2 _name_and_type_index;

  public:
    Constant_InvokeDynamic_info(Tag tag, u2 bootstrap_specifier_index,
                                u2 name_and_type_index)
        : Constant(tag), _bootstrap_specifier_index(bootstrap_specifier_index),
          _name_and_type_index(name_and_type_index) {
        assert(_tag == CONSTANT_InvokeDynamic_info);
    }
    u2 get_name_and_type_index() { return _name_and_type_index; }
};

class ConstantPool {
  private:
    friend class ClassFileParser;
    friend class ClassFilePartialParser;
    const u2 _cp_size;
    vector<Constant *> _constants;

  public:
    ConstantPool(u2 cp_size) : _cp_size(cp_size) {
        // Index 0 is unused
        _constants.resize(_cp_size);
    }

    ~ConstantPool() {
        for (int index = 1; index < _cp_size; index++) {
            delete (_constants[index]);
        }
    }

    const string symbol_at(int which);
};

#endif // JAVA_CONSTANTPOOL_HPP