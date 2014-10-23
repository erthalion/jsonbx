#ifndef __JSONBX_H__
#define __JSONBX_H__


typedef enum JsonbOutputKind {
    PrettyPrint = 0x10
} JsonbOutputKind;


#define choice_array(flag_val, a, b) flag_val == WJB_BEGIN_ARRAY ? a : b
#define choice_object(flag_val, a, b) flag_val == WJB_BEGIN_OBJECT ? a : b

#define is_array(flag_val, it) flag_val == WJB_BEGIN_ARRAY && !(*it)->isScalar

typedef bool (*walk_condition)(JsonbParseState**, JsonbValue*, uint32 /* token */, uint32 /* level */);

#endif
