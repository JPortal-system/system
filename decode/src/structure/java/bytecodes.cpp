#include "structure/java/bytecodes.hpp"
#include "structure/common.hpp"
#include <cassert>
#include <cstring>

using namespace std;

bool Bytecodes::_is_initialized = false;
Bytecodes::Code Bytecodes::_java_code[number_of_codes];
bool Bytecodes::_is_block_end[number_of_codes];
const char *Bytecodes::_name[number_of_codes];
u_char Bytecodes::_lengths[number_of_codes];

void Bytecodes::def(Code code, bool is_block_end, const char *name,
                    const char *format, const char *wide_format) {
    def(code, is_block_end, name, format, wide_format, code);
}

void Bytecodes::def(Code code, bool is_block_end, const char *name,
                    const char *format, const char *wide_format,
                    Code java_code) {
    assert(wide_format == nullptr || format != nullptr);
    int len = (format != nullptr ? (int)strlen(format) : 0);
    int wlen = (wide_format != nullptr ? (int)strlen(wide_format) : 0);
    _java_code[code] = java_code;
    _is_block_end[code] = is_block_end;
    _name[code] = name;
    _lengths[code] = (wlen << 4) | (len & 0xF);
}

static inline u4 get_u4(const u1 *buffer) {
    u1 ui[4];
    ui[3] = *buffer++;
    ui[2] = *buffer++;
    ui[1] = *buffer++;
    ui[0] = *buffer++;
    return *(u4 *)ui;
}

int Bytecodes::special_length_at(Code code, const u1 *buffer, const u4 offset) {
    // offset = buffer + 1 - _code
    switch (code) {
    case _breakpoint: {
        return 1;
    }
    case _wide: {
        return wide_length_for((Code) * (buffer + 1));
    }
    case _tableswitch: {
        jlong jintSize = sizeof(jint);
        jint alup = 1 + alignup(offset);
        buffer += alup;
        jlong lo = get_u4(buffer + jintSize);
        jlong hi = get_u4(buffer + 2 * jintSize);
        return alup + (3 + hi - lo + 1) * jintSize;
    }
    case _lookupswitch:
    case _fast_linearswitch:
    case _fast_binaryswitch: {
        long jintSize = sizeof(jint);
        jint alup = 1 + alignup(offset);
        buffer += alup;
        jlong npairs = get_u4(buffer + jintSize);
        return alup + (2 + 2 * npairs) * jintSize;
    }
    default: {
        // should not reach here
        return -1;
    }
    }
}

void Bytecodes::java_bytecode(Code &code, Code &follow_code) {
    follow_code = _illegal;
    if (code < number_of_java_codes)
        return;

    switch (code) {
    case (_fast_agetfield):
    case (_fast_bgetfield):
    case (_fast_cgetfield):
    case (_fast_dgetfield):
    case (_fast_fgetfield):
    case (_fast_igetfield):
    case (_fast_lgetfield):
    case (_fast_sgetfield):
        code = _getfield;
        return;

    case (_fast_aputfield):
    case (_fast_bputfield):
    case (_fast_zputfield):
    case (_fast_cputfield):
    case (_fast_dputfield):
    case (_fast_fputfield):
    case (_fast_iputfield):
    case (_fast_lputfield):
    case (_fast_sputfield):
        code = _putfield;
        return;

    case (_fast_aload_0):
        code = _aload_0;
        return;

    case (_fast_iaccess_0):
        code = _aload_0;
        follow_code = _getfield;
        return;

    case (_fast_aaccess_0):
        code = _aload_0;
        follow_code = _getfield;
        return;

    case (_fast_faccess_0):
        code = _aload_0;
        follow_code = _getfield;
        return;

    case (_fast_iload):
        code = _iload;
        return;

    case (_fast_iload2):
        code = _iload;
        follow_code = _iload;
        return;

    case (_fast_icaload):
        code = _iload;
        follow_code = _caload;
        return;

    case (_fast_invokevfinal):
        code = _invokevirtual;
        return;

    case (_fast_linearswitch):
        code = _lookupswitch;
        return;

    case (_fast_binaryswitch):
        code = _lookupswitch;
        return;

    case (_return_register_finalizer):
        code = _return;
        return;

    case (_invokehandle):
        code = _invokevirtual;
        return;

    case (_fast_aldc):
        code = _ldc;
        return;

    case (_fast_aldc_w):
        code = _ldc_w;
        return;

    case (_nofast_getfield):
        code = _getfield;
        return;

    case (_nofast_putfield):
        code = _putfield;
        return;

    case (_nofast_aload_0):
        code = _aload_0;
        return;

    case (_nofast_iload):
        code = _iload;
        return;

    default:
        break;
    }
    return;
}
void Bytecodes::initialize() {
    if (_is_initialized)
        return;
    assert(number_of_codes <= 256);

    // initialize bytecode tables - didn't use static array initializers
    // (such as {}) so we can do additional consistency checks and init-
    // code is independent of actual bytecode numbering.
    //
    // Note 1: nullptr for the format string means the bytecode doesn't exist
    //         in that form.
    //
    // Note 2: The result type is T_ILLEGAL for bytecodes where the top of stack
    //         type after execution is not only determined by the bytecode
    //         itself.

    //  Java bytecodes
    //  bytecode, is_block_end, bytecode_name, format, wide f.
    def(_nop, false, "nop", "b", nullptr);
    def(_aconst_null, false, "aconst_null", "b", nullptr);
    def(_iconst_m1, false, "iconst_m1", "b", nullptr);
    def(_iconst_0, false, "iconst_0", "b", nullptr);
    def(_iconst_1, false, "iconst_1", "b", nullptr);
    def(_iconst_2, false, "iconst_2", "b", nullptr);
    def(_iconst_3, false, "iconst_3", "b", nullptr);
    def(_iconst_4, false, "iconst_4", "b", nullptr);
    def(_iconst_5, false, "iconst_5", "b", nullptr);
    def(_lconst_0, false, "lconst_0", "b", nullptr);
    def(_lconst_1, false, "lconst_1", "b", nullptr);
    def(_fconst_0, false, "fconst_0", "b", nullptr);
    def(_fconst_1, false, "fconst_1", "b", nullptr);
    def(_fconst_2, false, "fconst_2", "b", nullptr);
    def(_dconst_0, false, "dconst_0", "b", nullptr);
    def(_dconst_1, false, "dconst_1", "b", nullptr);
    def(_bipush, false, "bipush", "bc", nullptr);
    def(_sipush, false, "sipush", "bcc", nullptr);
    def(_ldc, false, "ldc", "bk", nullptr);
    def(_ldc_w, false, "ldc_w", "bkk", nullptr);
    def(_ldc2_w, false, "ldc2_w", "bkk", nullptr);
    def(_iload, false, "iload", "bi", "wbii");
    def(_lload, false, "lload", "bi", "wbii");
    def(_fload, false, "fload", "bi", "wbii");
    def(_dload, false, "dload", "bi", "wbii");
    def(_aload, false, "aload", "bi", "wbii");
    def(_iload_0, false, "iload_0", "b", nullptr);
    def(_iload_1, false, "iload_1", "b", nullptr);
    def(_iload_2, false, "iload_2", "b", nullptr);
    def(_iload_3, false, "iload_3", "b", nullptr);
    def(_lload_0, false, "lload_0", "b", nullptr);
    def(_lload_1, false, "lload_1", "b", nullptr);
    def(_lload_2, false, "lload_2", "b", nullptr);
    def(_lload_3, false, "lload_3", "b", nullptr);
    def(_fload_0, false, "fload_0", "b", nullptr);
    def(_fload_1, false, "fload_1", "b", nullptr);
    def(_fload_2, false, "fload_2", "b", nullptr);
    def(_fload_3, false, "fload_3", "b", nullptr);
    def(_dload_0, false, "dload_0", "b", nullptr);
    def(_dload_1, false, "dload_1", "b", nullptr);
    def(_dload_2, false, "dload_2", "b", nullptr);
    def(_dload_3, false, "dload_3", "b", nullptr);
    def(_aload_0, false, "aload_0", "b", nullptr);
    def(_aload_1, false, "aload_1", "b", nullptr);
    def(_aload_2, false, "aload_2", "b", nullptr);
    def(_aload_3, false, "aload_3", "b", nullptr);
    def(_iaload, false, "iaload", "b", nullptr);
    def(_laload, false, "laload", "b", nullptr);
    def(_faload, false, "faload", "b", nullptr);
    def(_daload, false, "daload", "b", nullptr);
    def(_aaload, false, "aaload", "b", nullptr);
    def(_baload, false, "baload", "b", nullptr);
    def(_caload, false, "caload", "b", nullptr);
    def(_saload, false, "saload", "b", nullptr);
    def(_istore, false, "istore", "bi", "wbii");
    def(_lstore, false, "lstore", "bi", "wbii");
    def(_fstore, false, "fstore", "bi", "wbii");
    def(_dstore, false, "dstore", "bi", "wbii");
    def(_astore, false, "astore", "bi", "wbii");
    def(_istore_0, false, "istore_0", "b", nullptr);
    def(_istore_1, false, "istore_1", "b", nullptr);
    def(_istore_2, false, "istore_2", "b", nullptr);
    def(_istore_3, false, "istore_3", "b", nullptr);
    def(_lstore_0, false, "lstore_0", "b", nullptr);
    def(_lstore_1, false, "lstore_1", "b", nullptr);
    def(_lstore_2, false, "lstore_2", "b", nullptr);
    def(_lstore_3, false, "lstore_3", "b", nullptr);
    def(_fstore_0, false, "fstore_0", "b", nullptr);
    def(_fstore_1, false, "fstore_1", "b", nullptr);
    def(_fstore_2, false, "fstore_2", "b", nullptr);
    def(_fstore_3, false, "fstore_3", "b", nullptr);
    def(_dstore_0, false, "dstore_0", "b", nullptr);
    def(_dstore_1, false, "dstore_1", "b", nullptr);
    def(_dstore_2, false, "dstore_2", "b", nullptr);
    def(_dstore_3, false, "dstore_3", "b", nullptr);
    def(_astore_0, false, "astore_0", "b", nullptr);
    def(_astore_1, false, "astore_1", "b", nullptr);
    def(_astore_2, false, "astore_2", "b", nullptr);
    def(_astore_3, false, "astore_3", "b", nullptr);
    def(_iastore, false, "iastore", "b", nullptr);
    def(_lastore, false, "lastore", "b", nullptr);
    def(_fastore, false, "fastore", "b", nullptr);
    def(_dastore, false, "dastore", "b", nullptr);
    def(_aastore, false, "aastore", "b", nullptr);
    def(_bastore, false, "bastore", "b", nullptr);
    def(_castore, false, "castore", "b", nullptr);
    def(_sastore, false, "sastore", "b", nullptr);
    def(_pop, false, "pop", "b", nullptr);
    def(_pop2, false, "pop2", "b", nullptr);
    def(_dup, false, "dup", "b", nullptr);
    def(_dup_x1, false, "dup_x1", "b", nullptr);
    def(_dup_x2, false, "dup_x2", "b", nullptr);
    def(_dup2, false, "dup2", "b", nullptr);
    def(_dup2_x1, false, "dup2_x1", "b", nullptr);
    def(_dup2_x2, false, "dup2_x2", "b", nullptr);
    def(_swap, false, "swap", "b", nullptr);
    def(_iadd, false, "iadd", "b", nullptr);
    def(_ladd, false, "ladd", "b", nullptr);
    def(_fadd, false, "fadd", "b", nullptr);
    def(_dadd, false, "dadd", "b", nullptr);
    def(_isub, false, "isub", "b", nullptr);
    def(_lsub, false, "lsub", "b", nullptr);
    def(_fsub, false, "fsub", "b", nullptr);
    def(_dsub, false, "dsub", "b", nullptr);
    def(_imul, false, "imul", "b", nullptr);
    def(_lmul, false, "lmul", "b", nullptr);
    def(_fmul, false, "fmul", "b", nullptr);
    def(_dmul, false, "dmul", "b", nullptr);
    def(_idiv, false, "idiv", "b", nullptr);
    def(_ldiv, false, "ldiv", "b", nullptr);
    def(_fdiv, false, "fdiv", "b", nullptr);
    def(_ddiv, false, "ddiv", "b", nullptr);
    def(_irem, false, "irem", "b", nullptr);
    def(_lrem, false, "lrem", "b", nullptr);
    def(_frem, false, "frem", "b", nullptr);
    def(_drem, false, "drem", "b", nullptr);
    def(_ineg, false, "ineg", "b", nullptr);
    def(_lneg, false, "lneg", "b", nullptr);
    def(_fneg, false, "fneg", "b", nullptr);
    def(_dneg, false, "dneg", "b", nullptr);
    def(_ishl, false, "ishl", "b", nullptr);
    def(_lshl, false, "lshl", "b", nullptr);
    def(_ishr, false, "ishr", "b", nullptr);
    def(_lshr, false, "lshr", "b", nullptr);
    def(_iushr, false, "iushr", "b", nullptr);
    def(_lushr, false, "lushr", "b", nullptr);
    def(_iand, false, "iand", "b", nullptr);
    def(_land, false, "land", "b", nullptr);
    def(_ior, false, "ior", "b", nullptr);
    def(_lor, false, "lor", "b", nullptr);
    def(_ixor, false, "ixor", "b", nullptr);
    def(_lxor, false, "lxor", "b", nullptr);
    def(_iinc, false, "iinc", "bic", "wbiicc");
    def(_i2l, false, "i2l", "b", nullptr);
    def(_i2f, false, "i2f", "b", nullptr);
    def(_i2d, false, "i2d", "b", nullptr);
    def(_l2i, false, "l2i", "b", nullptr);
    def(_l2f, false, "l2f", "b", nullptr);
    def(_l2d, false, "l2d", "b", nullptr);
    def(_f2i, false, "f2i", "b", nullptr);
    def(_f2l, false, "f2l", "b", nullptr);
    def(_f2d, false, "f2d", "b", nullptr);
    def(_d2i, false, "d2i", "b", nullptr);
    def(_d2l, false, "d2l", "b", nullptr);
    def(_d2f, false, "d2f", "b", nullptr);
    def(_i2b, false, "i2b", "b", nullptr);
    def(_i2c, false, "i2c", "b", nullptr);
    def(_i2s, false, "i2s", "b", nullptr);
    def(_lcmp, false, "lcmp", "b", nullptr);
    def(_fcmpl, false, "fcmpl", "b", nullptr);
    def(_fcmpg, false, "fcmpg", "b", nullptr);
    def(_dcmpl, false, "dcmpl", "b", nullptr);
    def(_dcmpg, false, "dcmpg", "b", nullptr);
    def(_ifeq, true, "ifeq", "boo", nullptr);
    def(_ifne, true, "ifne", "boo", nullptr);
    def(_iflt, true, "iflt", "boo", nullptr);
    def(_ifge, true, "ifge", "boo", nullptr);
    def(_ifgt, true, "ifgt", "boo", nullptr);
    def(_ifle, true, "ifle", "boo", nullptr);
    def(_if_icmpeq, true, "if_icmpeq", "boo", nullptr);
    def(_if_icmpne, true, "if_icmpne", "boo", nullptr);
    def(_if_icmplt, true, "if_icmplt", "boo", nullptr);
    def(_if_icmpge, true, "if_icmpge", "boo", nullptr);
    def(_if_icmpgt, true, "if_icmpgt", "boo", nullptr);
    def(_if_icmple, true, "if_icmple", "boo", nullptr);
    def(_if_acmpeq, true, "if_acmpeq", "boo", nullptr);
    def(_if_acmpne, true, "if_acmpne", "boo", nullptr);
    def(_goto, true, "goto", "boo", nullptr);
    def(_jsr, true, "jsr", "boo", nullptr);
    def(_ret, true, "ret", "bi", "wbii");
    def(_tableswitch, true, "tableswitch", "", nullptr);
    def(_lookupswitch, true, "lookupswitch", "", nullptr);
    def(_ireturn, true, "ireturn", "b", nullptr);
    def(_lreturn, true, "lreturn", "b", nullptr);
    def(_freturn, true, "freturn", "b", nullptr);
    def(_dreturn, true, "dreturn", "b", nullptr);
    def(_areturn, true, "areturn", "b", nullptr);
    def(_return, true, "return", "b", nullptr);
    def(_getstatic, false, "getstatic", "bJJ", nullptr);
    def(_putstatic, false, "putstatic", "bJJ", nullptr);
    def(_getfield, false, "getfield", "bJJ", nullptr);
    def(_putfield, false, "putfield", "bJJ", nullptr);
    def(_invokevirtual, false, "invokevirtual", "bJJ", nullptr);
    def(_invokespecial, false, "invokespecial", "bJJ", nullptr);
    def(_invokestatic, false, "invokestatic", "bJJ", nullptr);
    def(_invokeinterface, false, "invokeinterface", "bJJ__", nullptr);
    def(_invokedynamic, false, "invokedynamic", "bJJJJ", nullptr);
    def(_new, false, "new", "bkk", nullptr);
    def(_newarray, false, "newarray", "bc", nullptr);
    def(_anewarray, false, "anewarray", "bkk", nullptr);
    def(_arraylength, false, "arraylength", "b", nullptr);
    def(_athrow, true, "athrow", "b", nullptr);
    def(_checkcast, false, "checkcast", "bkk", nullptr);
    def(_instanceof, false, "instanceof", "bkk", nullptr);
    def(_monitorenter, false, "monitorenter", "b", nullptr);
    def(_monitorexit, false, "monitorexit", "b", nullptr);
    def(_wide, false, "wide", "", nullptr);
    def(_multianewarray, false, "multianewarray", "bkkc", nullptr);
    def(_ifnull, true, "ifnull", "boo", nullptr);
    def(_ifnonnull, true, "ifnonnull", "boo", nullptr);
    def(_goto_w, true, "goto_w", "boooo", nullptr);
    def(_jsr_w, true, "jsr_w", "boooo", nullptr);
    def(_breakpoint, false, "breakpoint", "", nullptr);

    //  JVM bytecodes
    //  bytecode, is_jmp, bytecode_name, format, wide f., code

    def(_fast_agetfield, false, "fast_agetfield", "bJJ", nullptr, _getfield);
    def(_fast_bgetfield, false, "fast_bgetfield", "bJJ", nullptr, _getfield);
    def(_fast_cgetfield, false, "fast_cgetfield", "bJJ", nullptr, _getfield);
    def(_fast_dgetfield, false, "fast_dgetfield", "bJJ", nullptr, _getfield);
    def(_fast_fgetfield, false, "fast_fgetfield", "bJJ", nullptr, _getfield);
    def(_fast_igetfield, false, "fast_igetfield", "bJJ", nullptr, _getfield);
    def(_fast_lgetfield, false, "fast_lgetfield", "bJJ", nullptr, _getfield);
    def(_fast_sgetfield, false, "fast_sgetfield", "bJJ", nullptr, _getfield);

    def(_fast_aputfield, false, "fast_aputfield", "bJJ", nullptr, _putfield);
    def(_fast_bputfield, false, "fast_bputfield", "bJJ", nullptr, _putfield);
    def(_fast_zputfield, false, "fast_zputfield", "bJJ", nullptr, _putfield);
    def(_fast_cputfield, false, "fast_cputfield", "bJJ", nullptr, _putfield);
    def(_fast_dputfield, false, "fast_dputfield", "bJJ", nullptr, _putfield);
    def(_fast_fputfield, false, "fast_fputfield", "bJJ", nullptr, _putfield);
    def(_fast_iputfield, false, "fast_iputfield", "bJJ", nullptr, _putfield);
    def(_fast_lputfield, false, "fast_lputfield", "bJJ", nullptr, _putfield);
    def(_fast_sputfield, false, "fast_sputfield", "bJJ", nullptr, _putfield);

    def(_fast_aload_0, false, "fast_aload_0", "b", nullptr, _aload_0);
    def(_fast_iaccess_0, false, "fast_iaccess_0", "b_JJ", nullptr, _aload_0);
    def(_fast_aaccess_0, false, "fast_aaccess_0", "b_JJ", nullptr, _aload_0);
    def(_fast_faccess_0, false, "fast_faccess_0", "b_JJ", nullptr, _aload_0);

    def(_fast_iload, false, "fast_iload", "bi", nullptr, _iload);
    def(_fast_iload2, false, "fast_iload2", "bi_i", nullptr, _iload);
    def(_fast_icaload, false, "fast_icaload", "bi_", nullptr, _iload);

    // Faster method invocation.
    def(_fast_invokevfinal, false, "fast_invokevfinal", "bJJ", nullptr,
        _invokevirtual);

    def(_fast_linearswitch, false, "fast_linearswitch", "", nullptr,
        _lookupswitch);
    def(_fast_binaryswitch, false, "fast_binaryswitch", "", nullptr,
        _lookupswitch);

    def(_return_register_finalizer, false, "return_register_finalizer", "b",
        nullptr, _return);

    def(_invokehandle, false, "invokehandle", "bJJ", nullptr, _invokevirtual);

    def(_fast_aldc, false, "fast_aldc", "bj", nullptr, _ldc);
    def(_fast_aldc_w, false, "fast_aldc_w", "bJJ", nullptr, _ldc_w);

    def(_nofast_getfield, false, "nofast_getfield", "bJJ", nullptr, _getfield);
    def(_nofast_putfield, false, "nofast_putfield", "bJJ", nullptr, _putfield);

    def(_nofast_aload_0, false, "nofast_aload_0", "b", nullptr, _aload_0);
    def(_nofast_iload, false, "nofast_iload", "bi", nullptr, _iload);

    def(_shouldnotreachhere, false, "_shouldnotreachhere", "b", nullptr);

    // initialization successful
    _is_initialized = true;
}