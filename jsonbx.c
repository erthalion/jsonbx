#include "postgres.h"

#include "fmgr.h"
#include "catalog/pg_collation.h"
#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"
#include "miscadmin.h"

#include "jsonbx.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_print);
Datum jsonb_print(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_concat);
Datum jsonb_concat(PG_FUNCTION_ARGS);

char * JsonbToCStringExtended(StringInfo out, JsonbContainer in, int estimated_len, JsonbOutputKind kind);
static void printCR(StringInfo out, JsonbOutputKind kind);
static void printIndent(StringInfo out, JsonbOutputKind kind, int level);
static void jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal);

static JsonbValue * IteratorConcat(JsonbIterator **it1, JsonbIterator **it2, JsonbParseState **state);

Datum
jsonb_print(PG_FUNCTION_ARGS)
{
    Jsonb           *jb = PG_GETARG_JSONB(0);
    text            *out;
    int             flags = 0;
    StringInfo      str = makeStringInfo();

    if (PG_GETARG_BOOL(1))
        flags |= PrettyPrint;

    appendBinaryStringInfo(str, "    ", 4); /* VARHDRSZ */
    JsonbToCStringExtended(str, jb->root, VARSIZE(jb), flags);

    out = (text*)str->data;
    SET_VARSIZE(out, str->len);

    PG_RETURN_TEXT_P(out);
}


Datum
jsonb_concat(PG_FUNCTION_ARGS)
{
    Jsonb            *jb1 = PG_GETARG_JSONB(0);
    Jsonb            *jb2 = PG_GETARG_JSONB(1);
    Jsonb            *out = palloc(VARSIZE(jb1) + VARSIZE(jb2));
    JsonbParseState  *state = NULL;
    JsonbValue       *res;
    JsonbIterator    *it1, *it2;

    if (JB_ROOT_COUNT(jb1) == 0)
    {
        memcpy(out, jb2, VARSIZE(jb2));
        PG_RETURN_POINTER(out);
    }
    else if (JB_ROOT_COUNT(jb2) == 0) 
    {
        memcpy(out, jb1, VARSIZE(jb1));
        PG_RETURN_POINTER(out);
    }

    it1 = JsonbIteratorInit(&jb1->root);
    it2 = JsonbIteratorInit(&jb2->root);

    res = IteratorConcat(&it1, &it2, &state);

    if (res == NULL || (res->type == jbvArray && res->val.array.nElems == 0) ||
                       (res->type == jbvObject && res->val.object.nPairs == 0) )
    {
        SET_VARSIZE(out, VARHDRSZ);
    }
    else
    {
        if (res->type == jbvArray && res->val.array.nElems > 1)
            res->val.array.rawScalar = false;

        out = JsonbValueToJsonb(res);
    }

    PG_RETURN_POINTER(out);
}


/*
 * JsonbToCStringExtended
 *       Converts jsonb value to a C-string (take JsonbOutputKind into account).
 * See the original function JsonbToCString in the jsonb.c
 */
char *
JsonbToCStringExtended(StringInfo out, JsonbContainer in, int estimated_len, JsonbOutputKind kind)
{
    bool           first = true;
    JsonbIterator *it;
    int            type = 0;
    JsonbValue     v;
    int            level = 0;
    bool           redo_switch = false;

    if (out == NULL)
        out = makeStringInfo();

    enlargeStringInfo(out, (estimated_len >= 0) ? estimated_len : 64);

    it = JsonbIteratorInit(&in);

    while (redo_switch ||
           ((type = JsonbIteratorNext(&it, &v, false)) != WJB_DONE))
    {
        redo_switch = false;
        switch (type)
        {
            case WJB_BEGIN_ARRAY:
                if (!first)
                    appendBinaryStringInfo(out, ", ", 2);
                first = true;

                if (!v.val.array.rawScalar)
                    printCR(out, kind);
                    printIndent(out, kind, level);

                    appendStringInfoChar(out, '[');
                level++;

                printCR(out, kind);
                printIndent(out, kind, level);

                break;
            case WJB_BEGIN_OBJECT:
                if (!first)
                    appendBinaryStringInfo(out, ", ", 2);
                first = true;

                printCR(out, kind);
                printIndent(out, kind, level);

                appendStringInfoCharMacro(out, '{');

                level++;
                break;
            case WJB_KEY:
                if (!first)
                    appendBinaryStringInfo(out, ", ", 2);
                first = true;

                printCR(out, kind);
                printIndent(out, kind, level);

                /* json rules guarantee this is a string */
                jsonb_put_escaped_value(out, &v);
                appendBinaryStringInfo(out, ": ", 2);

                type = JsonbIteratorNext(&it, &v, false);
                if (type == WJB_VALUE)
                {
                    first = false;
                    jsonb_put_escaped_value(out, &v);
                }
                else
                {
                    Assert(type == WJB_BEGIN_OBJECT || type == WJB_BEGIN_ARRAY);

                    /*
                     * We need to rerun the current switch() since we need to
                     * output the object which we just got from the iterator
                     * before calling the iterator again.
                     */
                    redo_switch = true;
                }
                break;
            case WJB_ELEM:
                if (!first) {
                    appendBinaryStringInfo(out, ", ", 2);

                    printCR(out, kind);
                    printIndent(out, kind, level);
                }
                else {
                    first = false;
                }

                jsonb_put_escaped_value(out, &v);
                break;
            case WJB_END_ARRAY:
                level--;

                printCR(out, kind);
                printIndent(out, kind, level);

                if (!v.val.array.rawScalar)
                    appendStringInfoChar(out, ']');
                first = false;
                break;
            case WJB_END_OBJECT:
                level--;

                printCR(out, kind);
                printIndent(out, kind, level);

                appendStringInfoCharMacro(out, '}');
                first = false;
                break;
            default:
                elog(ERROR, "unknown flag of jsonb iterator");
        }
    }

    Assert(level == 0);

    return out->data;
}


static void
printCR(StringInfo out, JsonbOutputKind kind)
{
    if (kind & PrettyPrint)
        appendBinaryStringInfo(out, "    ", 4);
        appendStringInfoCharMacro(out, '\n');
}

static void
printIndent(StringInfo out, JsonbOutputKind kind, int level)
{
    if (kind & PrettyPrint)
    {
        int i;
        for(i=0; i<4*level; i++)
            appendStringInfoCharMacro(out, ' ');
    }
}


static void
jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal)
{
    switch (scalarVal->type)
    {
        case jbvNull:
            appendBinaryStringInfo(out, "null", 4);
            break;
        case jbvString:
            escape_json(out, pnstrdup(scalarVal->val.string.val, scalarVal->val.string.len));
            break;
        case jbvNumeric:
            appendStringInfoString(out,
                                   DatumGetCString(DirectFunctionCall1(numeric_out,
                                                                       PointerGetDatum(scalarVal->val.numeric))));
            break;
        case jbvBool:
            if (scalarVal->val.boolean)
                appendBinaryStringInfo(out, "true", 4);
            else
                appendBinaryStringInfo(out, "false", 5);
            break;
        default:
            elog(ERROR, "unknown jsonb scalar type");
    }
}


static JsonbValue *
IteratorConcat(JsonbIterator **it1, JsonbIterator **it2,
        JsonbParseState **state)
{
    uint32            r1, r2, rk1, rk2;
    JsonbValue        v1, v2, *res = NULL;

    r1 = rk1 = JsonbIteratorNext(it1, &v1, false);
    r2 = rk2 = JsonbIteratorNext(it2, &v2, false);

    if (rk1 == WJB_BEGIN_OBJECT && rk2 == WJB_BEGIN_OBJECT)
    {
        int             level = 1;

        res = pushJsonbValue(state, r1, &v1);
        while((r1 = JsonbIteratorNext(it1, &v1, false)) != 0)
        {
            if (r1 == WJB_BEGIN_OBJECT) {
                ++level;
            }
            else if (r1 == WJB_END_OBJECT) {
                --level;
            }

            if (level != 0) {
                res = pushJsonbValue(state, r1, &v1);
            }
        }

        while((r2 = JsonbIteratorNext(it2, &v2, false)) != 0)
        {
            res = pushJsonbValue(state, r2, &v2);
        }
    }
    else if ((rk1 == WJB_BEGIN_OBJECT || rk1 == WJB_BEGIN_ARRAY) &&
             (rk2 == WJB_BEGIN_OBJECT || rk2 == WJB_BEGIN_ARRAY))
    {
        res = pushJsonbValue(state, r1, &v1);
        for(;;)
        {
            r1 = JsonbIteratorNext(it1, &v1, true);
            if (r1 == WJB_END_OBJECT || r1 == WJB_END_ARRAY)
                break;
            Assert(r1 == WJB_KEY || r1 == WJB_VALUE || r1 == WJB_ELEM);
            pushJsonbValue(state, r1, &v1);
        }

        while((r2 = JsonbIteratorNext(it2, &v2, true)) != 0)
        {
            if (!(r2 == WJB_END_OBJECT || r2 == WJB_END_ARRAY))
            {
                if (rk1 == WJB_BEGIN_OBJECT)
                {
                    pushJsonbValue(state, WJB_KEY, &v2);
                    r2 = JsonbIteratorNext(it2, &v2, true);
                    Assert(r2 == WJB_ELEM);
                    pushJsonbValue(state, WJB_VALUE, &v2);
                }
                else
                {
                    pushJsonbValue(state, WJB_ELEM, &v2);
                }
            }
        }

        res = pushJsonbValue(state,
                              (rk1 == WJB_BEGIN_OBJECT) ? WJB_END_OBJECT : WJB_END_ARRAY,
                              NULL/* signal to sort */);
    }
    else if ((rk1 & (WJB_VALUE | WJB_ELEM)) != 0)
    {
        if (v2.type == jbvArray && v2.val.array.rawScalar)
        {
            Assert(v2.val.array.nElems == 1);
            r2 = JsonbIteratorNext(it2, &v2, false);
            pushJsonbValue(state, r1, &v2);
        }
        else
        {
            res = pushJsonbValue(state, r2, &v2);
            while((r2 = JsonbIteratorNext(it2, &v2, true)) != 0)
                res = pushJsonbValue(state, r2, &v2);
        }
    }
    else
    {
        elog(ERROR, "invalid concatnation of hstores");
    }

    return res;
}
