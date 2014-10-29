#ifndef __JSONBX_H__
#define __JSONBX_H__


typedef enum JsonbOutputKind {
    PrettyPrint = 0x10
} JsonbOutputKind;


#define choice_array(flag_val, a, b) flag_val == WJB_BEGIN_ARRAY ? a : b
#define choice_object(flag_val, a, b) flag_val == WJB_BEGIN_OBJECT ? a : b

#define is_array(flag_val, it) flag_val == WJB_BEGIN_ARRAY && !(*it)->isScalar

typedef bool (*walk_condition)(JsonbParseState**, JsonbValue*, uint32 /* token */, uint32 /* level */);

extern char * JsonbToCStringExtended(StringInfo out, JsonbContainer *in, int estimated_len, JsonbOutputKind kind);
extern void printCR(StringInfo out, JsonbOutputKind kind);
extern void printIndent(StringInfo out, JsonbOutputKind kind, int level);
extern void jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal);
extern JsonbValue* replacePath(JsonbIterator **it, Datum *path_elems, bool *path_nulls, int path_len,
        JsonbParseState  **st, int level, JsonbValue *newval);
extern bool h_atoi(char *c, int l, int *acc);

extern JsonbValue * IteratorConcat(JsonbIterator **it1, JsonbIterator **it2, JsonbParseState **state);
extern JsonbValue* walkJsonb(JsonbIterator **it, JsonbValue *v, JsonbParseState **state, walk_condition);
extern bool untilLast(JsonbParseState **state, JsonbValue *v, uint32 token, uint32 level);

#endif
