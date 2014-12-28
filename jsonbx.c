#include "postgres.h"

#include "catalog/pg_type.h"
#include "utils/jsonb.h"

#include "jsonbx.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_print);
Datum jsonb_print(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_concat);
Datum jsonb_concat(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_delete);
Datum jsonb_delete(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_delete_idx);
Datum jsonb_delete_idx(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_replace);
Datum jsonb_replace(PG_FUNCTION_ARGS);

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
    JsonbToCStringExtended(str, &jb->root, VARSIZE(jb), flags);

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

    /*
     * If one of the jsonb is empty,
     * just return other.
     */
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


Datum
jsonb_delete(PG_FUNCTION_ARGS)
{
    Jsonb              *in = PG_GETARG_JSONB(0);
    text               *key = PG_GETARG_TEXT_PP(1);
    char               *keyptr = VARDATA_ANY(key);
    int                 keylen = VARSIZE_ANY_EXHDR(key);
    Jsonb              *out = palloc(VARSIZE(in));
    JsonbParseState    *state = NULL;
    JsonbIterator      *it;
    uint32              r;
    JsonbValue          v, *res = NULL;
    bool                skipNested = false;

    SET_VARSIZE(out, VARSIZE(in));

    if (JB_ROOT_COUNT(in) == 0)
    {
        PG_RETURN_POINTER(out);
    }

    it = JsonbIteratorInit(&in->root);

    while((r = JsonbIteratorNext(&it, &v, skipNested)) != 0)
    {
        skipNested = true;

        if ((r == WJB_ELEM || r == WJB_KEY) &&
            (v.type == jbvString && keylen == v.val.string.len &&
             memcmp(keyptr, v.val.string.val, keylen) == 0))
        {
            if (r == WJB_KEY)
            {
                /* skip corresponding value */
                JsonbIteratorNext(&it, &v, true);
            }

            continue;
        }

        res = pushJsonbValue(&state, r, &v);
    }

    if (res == NULL || (res->type == jbvArray && res->val.array.nElems == 0) ||
                       (res->type == jbvObject && res->val.object.nPairs == 0) )
    {
        SET_VARSIZE(out, VARHDRSZ);
    }
    else
    {
        out = JsonbValueToJsonb(res);
    }

    PG_RETURN_POINTER(out);
}


Datum
jsonb_delete_idx(PG_FUNCTION_ARGS)
{
    Jsonb            *in = PG_GETARG_JSONB(0);
    int               idx = PG_GETARG_INT32(1);
    Jsonb            *out = palloc(VARSIZE(in));
    JsonbParseState  *state = NULL;
    JsonbIterator    *it;
    uint32            r, i = 0, n;
    JsonbValue        v, *res = NULL;

    if (JB_ROOT_COUNT(in) == 0)
    {
        memcpy(out, in, VARSIZE(in));
        PG_RETURN_POINTER(out);
    }

    it = JsonbIteratorInit(&in->root);

    r = JsonbIteratorNext(&it, &v, false);
    if (r == WJB_BEGIN_ARRAY)
        n = v.val.array.nElems;
    else
        n = v.val.object.nPairs;

    if (idx < 0)
    {
        if (-idx > n)
            idx = n;
        else
            idx = n + idx;
    }

    if (idx >= n)
    {
        memcpy(out, in, VARSIZE(in));
        PG_RETURN_POINTER(out);
    }

    pushJsonbValue(&state, r, &v);

    while((r = JsonbIteratorNext(&it, &v, true)) != 0)
    {
        if (r == WJB_ELEM || r == WJB_KEY)
        {
            if (i++ == idx)
            {
                if (r == WJB_KEY)
                    JsonbIteratorNext(&it, &v, true); /* skip value */
                continue;
            }
        }

        res = pushJsonbValue(&state, r, &v);
    }

    if (res == NULL || (res->type == jbvArray && res->val.array.nElems == 0) ||
                       (res->type == jbvObject && res->val.object.nPairs == 0) )
    {
        SET_VARSIZE(out, VARHDRSZ);
    }
    else
    {
        out = JsonbValueToJsonb(res);
    }

    PG_RETURN_POINTER(out);
}

Datum
jsonb_replace(PG_FUNCTION_ARGS)
{
    Jsonb              *in = PG_GETARG_JSONB(0);
    ArrayType          *path = PG_GETARG_ARRAYTYPE_P(1);
    Jsonb              *newval = PG_GETARG_JSONB(2);
    Jsonb              *out = palloc(VARSIZE(in) + VARSIZE(newval));
    JsonbValue         *res = NULL;
    JsonbValue          value;
    Datum              *path_elems;
    bool               *path_nulls;
    int                 path_len;
    JsonbIterator      *it;
    JsonbParseState    *st = NULL;

    Assert(ARR_ELEMTYPE(path) == TEXTOID);

    if (ARR_NDIM(path) > 1)
        ereport(ERROR,
                (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
                 errmsg("wrong number of array subscripts")));

    if (JB_ROOT_COUNT(in) == 0)
    {
        memcpy(out, in, VARSIZE(in));
        PG_RETURN_POINTER(out);
    }

    deconstruct_array(path, TEXTOID, -1, false, 'i',
                      &path_elems, &path_nulls, &path_len);

    if (path_len == 0)
    {
        memcpy(out, in, VARSIZE(in));
        PG_RETURN_POINTER(out);
    }

    if (JB_ROOT_COUNT(newval) == 0)
    {
        value.type = jbvNull;
    }
    else
    {
        uint32 r, is_scalar = false;
        it = JsonbIteratorInit(&newval->root);
        is_scalar = it->isScalar;
        while((r = JsonbIteratorNext(&it, &value, false)) != 0)
        {
            res = pushJsonbValue(&st, r, &value);
        }
        
        if (is_scalar && res->type == jbvArray)
        {
            value = res->val.array.elems[0];
        }
        else
        {
            value = *res;
        }
    }

    it = JsonbIteratorInit(&in->root);

    res = replacePath(&it, path_elems, path_nulls, path_len, &st, 0, &value);

    if (res == NULL)
    {
        SET_VARSIZE(out, VARHDRSZ);
    }
    else
    {
        out = JsonbValueToJsonb(res);
    }

    PG_RETURN_POINTER(out);
}
