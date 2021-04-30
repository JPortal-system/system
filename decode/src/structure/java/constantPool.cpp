#include "structure/java/constantPool.hpp"

const string ConstantPool::symbol_at(int which) {
    switch (_constants[which]->tag()) {
    case CONSTANT_Class_info: {
        return symbol_at(
            ((Constant_Class_info *)_constants[which])->get_name_index());
    }
    case CONSTANT_Utf8_info: {
        return ((Constant_Utf8_info *)_constants[which])->str();
    }
    default: {
        return nullptr;
    }
    }
}