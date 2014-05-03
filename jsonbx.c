#include "postgres.h"

#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"

#include "jsonbx.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_print);

Datum jsonb_print(PG_FUNCTION_ARGS);
char *JsonbToCStringExtended(StringInfo out, char *in, int estimated_len, JsonbOutputKind kind);
static void printCR(StringInfo out, JsonbOutputKind kind);
static void printIndent(StringInfo out, JsonbOutputKind kind, int level);
static void jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal);

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
    JsonbToCStringExtended(str, VARDATA(jb), VARSIZE(jb), flags);

    out = (text*)str->data;
    SET_VARSIZE(out, str->len);

    PG_RETURN_TEXT_P(out);
}


/*
 * JsonbToCStringExtended
 *       Converts jsonb value to a C-string (take JsonbOutputKind into account).
 * See the original function JsonbToCString in the jsonb.c
 */
char *
JsonbToCStringExtended(StringInfo out, JsonbSuperHeader in, int estimated_len, JsonbOutputKind kind)
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

    it = JsonbIteratorInit(in);

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
