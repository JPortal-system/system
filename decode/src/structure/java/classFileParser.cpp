#include "structure/java/classFileParser.hpp"

#define JAVA_CLASSFILE_MAGIC 0xCAFEBABE
#define JAVA_MIN_SUPPORTED_VERSION 45
#define JAVA_PREVIEW_MINOR_VERSION 65535

// Used for two backward compatibility reasons:
// - to check for new additions to the class file format in JDK1.5
// - to check for bug fixes in the format checker in JDK1.5
#define JAVA_1_5_VERSION 49

// Used for backward compatibility reasons:
// - to check for javac bug fixes that happened after 1.5
// - also used as the max version when running in jdk6
#define JAVA_6_VERSION 50

// Used for backward compatibility reasons:
// - to disallow argument and require ACC_STATIC for <clinit> methods
#define JAVA_7_VERSION 51

// Extension method support.
#define JAVA_8_VERSION 52

#define JAVA_9_VERSION 53

#define JAVA_10_VERSION 54

#define JAVA_11_VERSION 55

#define JAVA_12_VERSION 56

// Class file format tags
#define TAG_CODE "Code"

ClassFileParser::ClassFileParser(string file_path, Klass &klass)
    : _klass(klass) {
    // check if file exists
    struct stat st;
    if (stat(file_path.c_str(), &st) == 0) {
        // found file, open it
        ifstream file_handle(file_path);
        if (file_handle.is_open()) {
            // read contents into resource array
            _buffer = (u1 *)malloc(st.st_size * sizeof(u1));
            file_handle.read((char *)_buffer, st.st_size);
            assert(file_handle.peek() == EOF);
            // close file
            file_handle.close();
            _stream = new BufferStream(_buffer, st.st_size);
        }
    }
}

ClassFileParser::~ClassFileParser() {
    if (_stream != nullptr) {
        delete (_stream);
    }
    if (_buffer != nullptr) {
        delete (_buffer);
    }
    if (_cp != nullptr) {
        delete (_cp);
    }
}

void ClassFileParser::parse_class() {
    const BufferStream *const stream = _stream;
    assert(stream != NULL);

    // Magic value
    stream->skip_u4(1);

    // Version numbers
    _minor_version = stream->get_u2();
    _major_version = stream->get_u2();

    u2 cp_size = stream->get_u2();

    _cp = new ConstantPool(cp_size);

    ConstantPool *const cp = _cp;
    // cout << "before parse_constant_pool" << endl;
    parse_constant_pool(stream, cp, cp_size);
    // cout << "after parse_constant_pool" << endl;
    // Access flags
    jint flags = stream->get_u2();

    // This class
    _this_class_index = stream->get_u2();
    const string class_name = cp->symbol_at(_this_class_index);
    // cout << _klass.get_name() << " class " << class_name << endl;

    // Super class
    _super_class_index = stream->get_u2();
    if (_super_class_index){
        const string super_class_name = cp->symbol_at(_super_class_index);
        // cout << " super_class " << super_class_name << endl;
        _klass.set_father_name(super_class_name);
    }
    // Skip Interfaces
    u2 itfs_len = stream->get_u2();
    for (u2 i = 0; i < itfs_len; ++i) {
        u2 interface_index = stream->get_u2();
        const string interface_name = cp->symbol_at(interface_index);
        // cout << " interface " << interface_name << endl;
        _klass.add_interface_name(interface_name);
    }

    // Skip Fields
    parse_fields(stream, cp);

    // Methods
    parse_methods(stream, cp);

    // Additional attributes/annotations
    parse_classfile_attributes(stream);

    //  Make sure this is the end of class file stream
    // assert(stream->at_eos());

    return;
}

void ClassFileParser::parse_constant_pool(const BufferStream *const stream,
                                          ConstantPool *const cp,
                                          const int length) {
    assert(stream != NULL);
    assert(cp != NULL);
    // cout << length << endl;
    int index = 1;
    // parsing  Index 0 is unused
    for (index = 1; index < length; ++index) {
        const u1 tag = stream->get_u1();
        switch (tag) {
        case CONSTANT_Class_info: {
            const u2 name_index = stream->get_u2();
            // cout << index << ":Class " << name_index << endl;
            cp->_constants[index] =
                new Constant_Class_info(CONSTANT_Class_info, name_index);
            break;
        }
        case CONSTANT_Fieldref_info: {
            const u2 class_index = stream->get_u2();
            const u2 name_and_type_index = stream->get_u2();
            // cout << index << ":Fieldref " << class_index << " "
            //      << name_and_type_index << endl;
            cp->_constants[index] = new Constant_Fieldref_info(
                CONSTANT_Fieldref_info, class_index, name_and_type_index);
            break;
        }
        case CONSTANT_Methodref_info: {
            const u2 class_index = stream->get_u2();
            const u2 name_and_type_index = stream->get_u2();
            // cout << index << ":Methodref " << class_index << " "
            //      << name_and_type_index << endl;
            cp->_constants[index] = new Constant_Methodref_info(
                CONSTANT_Methodref_info, class_index, name_and_type_index);
            break;
        }
        case CONSTANT_InterfaceMethodref_info: {
            const u2 class_index = stream->get_u2();
            const u2 name_and_type_index = stream->get_u2();
            // cout << index << ":InterfaceMethodref" << class_index << " "
            //      << name_and_type_index << endl;
            cp->_constants[index] = new Constant_InterfaceMethodref_info(
                CONSTANT_InterfaceMethodref_info, class_index,
                name_and_type_index);
            break;
        }
        case CONSTANT_String_info: {
            const u2 string_index = stream->get_u2();
            // cout << index << ":String " << string_index << endl;
            cp->_constants[index] =
                new Constant_String_info(CONSTANT_String_info, string_index);
            break;
        }
        case CONSTANT_MethodHandle_info: {
            const u1 ref_kind = stream->get_u1();
            const u2 method_index = stream->get_u2();
            // cout << index << ":MethodHandle " << ref_kind << " " <<
            // method_index
            //      << endl;
            cp->_constants[index] = new Constant_MethodHandle_info(
                CONSTANT_MethodHandle_info, ref_kind, method_index);
            break;
        }
        case CONSTANT_MethodType_info: {
            const u2 signature_index = stream->get_u2();
            // cout << index << ":MethodType " << signature_index << endl;
            cp->_constants[index] = new Constant_MethodType_info(
                CONSTANT_MethodType_info, signature_index);
            break;
        }
        case CONSTANT_Dynamic_info: {
            const u2 bootstrap_specifier_index = stream->get_u2();
            const u2 name_and_type_index = stream->get_u2();
            // cout << index << ":Dynamic " << bootstrap_specifier_index << " "
            //      << name_and_type_index << endl;
            cp->_constants[index] = new Constant_Dynamic_info(
                CONSTANT_Dynamic_info, bootstrap_specifier_index,
                name_and_type_index);
            break;
        }
        case CONSTANT_InvokeDynamic_info: {
            const u2 bootstrap_specifier_index = stream->get_u2();
            const u2 name_and_type_index = stream->get_u2();
            // cout << index << ":InvokeDynamic " << bootstrap_specifier_index
            //      << " " << name_and_type_index << endl;
            cp->_constants[index] = new Constant_InvokeDynamic_info(
                CONSTANT_InvokeDynamic_info, bootstrap_specifier_index,
                name_and_type_index);
            break;
        }
        case CONSTANT_Integer_info: {
            const u4 bytes = stream->get_u4();
            // cout << index << ":Integer " << bytes << endl;
            cp->_constants[index] =
                new Constant_Integer_info(CONSTANT_Integer_info, bytes);
            break;
        }
        case CONSTANT_Float_info: {
            const u4 bytes = stream->get_u4();
            // cout << index << ":Float " << bytes << endl;
            cp->_constants[index] =
                new Constant_Float_info(CONSTANT_Float_info, bytes);
            break;
        }
        case CONSTANT_Long_info: {
            const u8 bytes = stream->get_u8();
            // cout << index << ":Long " << bytes << endl;
            cp->_constants[index] =
                new Constant_Long_info(CONSTANT_Long_info, bytes);
            ++index;
            break;
        }
        case CONSTANT_Double_info: {
            const u8 bytes = stream->get_u8();
            // cout << index << ":Double " << bytes << endl;
            cp->_constants[index] =
                new Constant_Double_info(CONSTANT_Double_info, bytes);
            ++index;
            break;
        }
        case CONSTANT_NameAndType_info: {
            const u2 name_index = stream->get_u2();
            const u2 signature_index = stream->get_u2();
            // cout << index << ":NameAndType " << name_index << " "
            //      << signature_index << endl;
            cp->_constants[index] = new Constant_NameAndType_info(
                CONSTANT_NameAndType_info, name_index, signature_index);
            break;
        }
        case CONSTANT_Utf8_info: {
            // Only handle one byte Utf8 (same as ASCII)
            // TODO
            const u2 str_len = stream->get_u2();
            char *buffer = (char *)stream->current();
            char *str = (char *)malloc(str_len * sizeof(char) + 1);
            strncpy(str, buffer, str_len);
            str[str_len] = '\0';
            // cout << index << ":Utf8 " << str << endl;
            cp->_constants[index] =
                new Constant_Utf8_info(CONSTANT_Utf8_info, str_len, str);
            stream->skip_u1(str_len);
            break;
        }
        case 19:
        case 20: {
            // cout << index << ":19/20 " << endl;
            if (_major_version >= JAVA_9_VERSION) {
                u2 name_index = stream->get_u2();
                cp->_constants[index] =
                new Constant_Class_info(CONSTANT_Class_info, name_index);
                break;
            }
        }
        default: {
            // should not reach here
        }
        } // end of switch(tag)
    }     // end of for

    for (index = 1; index < length; ++index) {
        // cout << index << endl;
        auto tag = cp->_constants[index]->tag();
        if (CONSTANT_Methodref_info == tag ||
            CONSTANT_InterfaceMethodref_info == tag) {
            auto methodref = (Constant_Methodref_info *)cp->_constants[index];
            // cout << methodref->get_class_index() << endl;
            auto class_info =
                (Constant_Class_info
                     *)(cp->_constants[methodref->get_class_index()]);
            string class_name =
                ((Constant_Utf8_info
                      *)(cp->_constants[class_info->get_name_index()]))
                    ->str();
            // cout << methodref->get_name_and_type_index() << endl;
            auto name_and_type =
                (Constant_NameAndType_info
                     *)(cp->_constants[methodref->get_name_and_type_index()]);
            // cout << name_and_type->get_name_index() << endl;
            string name =
                ((Constant_Utf8_info
                      *)(cp->_constants[name_and_type->get_name_index()]))
                    ->str();
            // cout << name_and_type->get_type_index() << endl;
            string type =
                ((Constant_Utf8_info
                      *)(cp->_constants[name_and_type->get_type_index()]))
                    ->str();
            _klass.insert_method_ref(index, class_name + '.' + name + type);
            // cout << index << endl;
        } else if (CONSTANT_Dynamic_info == tag || CONSTANT_InvokeDynamic_info == tag) {
            auto dynamicinfo = (Constant_InvokeDynamic_info*)(cp->_constants[index]);
            // cout << methodref->get_name_and_type_index() << endl;
            auto name_and_type =
                (Constant_NameAndType_info
                     *)(cp->_constants[dynamicinfo->get_name_and_type_index()]);
            // cout << name_and_type->get_name_index() << endl;
            string name =
                ((Constant_Utf8_info
                      *)(cp->_constants[name_and_type->get_name_index()]))
                    ->str();
            // cout << name_and_type->get_type_index() << endl;
            string type =
                ((Constant_Utf8_info
                      *)(cp->_constants[name_and_type->get_type_index()]))
                    ->str();
            _klass.insert_method_ref(index, name + type);
        } else if (CONSTANT_Long_info == tag || CONSTANT_Double_info == tag) {
            ++index;
        }
    }
}

void ClassFileParser::parse_fields(const BufferStream *const stream,
                                   ConstantPool *cp) {
    assert(stream != NULL);
    const u2 length = stream->get_u2();
    for (int n = 0; n < length; n++) {
        // access_flags, name_index, descriptor_index, attributes_count
        const jint flags = stream->get_u2();

        const u2 name_index = stream->get_u2();
        const u2 signature_index = stream->get_u2();

        u2 attributes_count = stream->get_u2();

        while (attributes_count--) {
            const u2 attribute_name_index = stream->get_u2();
            const u4 attribute_length = stream->get_u4();
            stream->skip_u1(attribute_length); // Skip attribute
        }

    } // end of for
}

// Parse methods
void ClassFileParser::parse_methods(const BufferStream *const stream,
                                    ConstantPool *cp) {
    assert(stream != NULL);
    const u2 length = stream->get_u2();
    for (int index = 0; index < length; index++) {
        _klass.insert_method_map(parse_method(stream, cp));
    } // End of for
}

Method *ClassFileParser::parse_method(const BufferStream *const stream,
                                      ConstantPool *cp) {
    assert(stream != NULL);

    int flags = stream->get_u2();
    const u2 name_index = stream->get_u2();
    const string method_name = cp->symbol_at(name_index);

    const u2 signature_index = stream->get_u2();
    const string method_signature = cp->symbol_at(signature_index);

    u2 max_stack = 0;
    u2 max_locals = 0;
    u4 code_length = 0;
    const u1 *code_start = nullptr;
    u2 exception_table_length = 0;
    const u1 *exception_table = nullptr;

    u2 method_attributes_count = stream->get_u2();
    while (method_attributes_count--) {
        const u2 method_attribute_name_index = stream->get_u2();
        const u4 method_attribute_length = stream->get_u4();

        const string method_attribute_name =
            cp->symbol_at(method_attribute_name_index);
        if (0 == method_attribute_name.compare(TAG_CODE)) {
            // Stack size, locals size, and code size
            if (_major_version == 45 && _minor_version <= 2) {
                max_stack = stream->get_u1();
                max_locals = stream->get_u1();
                code_length = stream->get_u2();
            } else {
                max_stack = stream->get_u2();
                max_locals = stream->get_u2();
                code_length = stream->get_u4();
            }
            // Code pointer
            code_start = stream->current();
            stream->skip_u1(code_length);

            // Exception handler table
            // Not parse exception table
            exception_table_length = stream->get_u2();
            exception_table = stream->current();
            stream->skip_u2(exception_table_length * 4);

            // Additional attributes in code attribute
            // Not parse
            u2 code_attributes_count = stream->get_u2();
            while (code_attributes_count--) {
                const u2 code_attribute_name_index = stream->get_u2();
                const u4 code_attribute_length = stream->get_u4();
                stream->skip_u1(code_attribute_length);
            }
        } else {
            stream->skip_u1(method_attribute_length);
        }
    } // End of while
    // cout << "  " << method_name << method_signature << ": " << code_length
    //     << endl;
    return new Method(method_name + method_signature, code_start, code_length,
                      exception_table, exception_table_length, _klass);
}

// Classfile attribute parsing
void ClassFileParser::parse_classfile_attributes(
    const BufferStream *const stream) {
    assert(stream != NULL);

    u2 attributes_count = stream->get_u2();

    while (attributes_count--) {
        const u2 attribute_name_index = stream->get_u2();
        const u4 attribute_length = stream->get_u4();
        stream->skip_u1(attribute_length); // Skip attribute
    }
}