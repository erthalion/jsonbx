#include "postgres.h"

#include "catalog/pg_type.h"
#include "utils/jsonb.h"
#include "fmgr.h"
#include "access/htup_details.h"
#include "utils/typcache.h"
#include "catalog/pg_type.h"
#include "utils/lsyscache.h"

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

PG_FUNCTION_INFO_V1(jsonb_agg_transfn);
Datum jsonb_agg_transfn(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_agg_finalfn);
Datum jsonb_agg_finalfn(PG_FUNCTION_ARGS);

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

Datum
jsonb_agg_transfn(PG_FUNCTION_ARGS)
{
    Oid                 val_type = get_fn_expr_argtype(fcinfo->flinfo, 1);
    MemoryContext       aggcontext, oldcontext;
    JsonbParseState     *state = NULL;
    Datum               val;
    Oid                 outfuncoid;
    JsonbValue          *res = NULL;

    HeapTupleHeader     td;
    Oid                 tupType;
    int32               tupTypmod;
    TupleDesc           tupdesc;
    HeapTupleData       tmptup, *tuple;

    if (val_type == InvalidOid)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("could not determine input data type")));

    if (!AggCheckCallContext(fcinfo, &aggcontext))
    {
        /* cannot be called directly because of internal-type argument */
        elog(ERROR, "json_agg_transfn called in non-aggregate context");
    }

    if (PG_ARGISNULL(0))
    {
        /*
         * Make this StringInfo in a context where it will persist for the
         * duration of the aggregate call.  MemoryContextSwitchTo is only
         * needed the first time, as the StringInfo routines make sure they
         * use the right context to enlarge the object if necessary.
         */
        oldcontext = MemoryContextSwitchTo(aggcontext);
        pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);
        MemoryContextSwitchTo(oldcontext);
    }
    else
    {
        state = (JsonbParseState*) PG_GETARG_POINTER(0);
    }

    /* fast path for NULLs */
    if (PG_ARGISNULL(1))
    {
        datum_to_jsonb((Datum) 0, true, state, jbvNull, InvalidOid, false);
        PG_RETURN_POINTER(state);
    }


    val = PG_GETARG_DATUM(1);

    if (type_is_rowtype(getBaseType(val_type)))
    {
        td = DatumGetHeapTupleHeader(val);
        /*tupType = HeapTupleHeaderGetTypeId(td);*/
        /*tupTypmod = HeapTupleHeaderGetTypMod(td);*/
        /*tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);*/

        /*tmptup.t_len = HeapTupleHeaderGetDatumLength(td);*/
        /*tmptup.t_data = td;*/
        /*tuple = &tmptup;*/

        // new dummy jbvObject
        JsonbValue field_key;
        JsonbValue field_value;

        pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);

        field_key.type = jbvString;
        field_key.val.string.val = "test1";
        field_key.val.string.len = strlen("test1");
        res = pushJsonbValue(&state, WJB_KEY, &field_key);

        field_value.type = jbvBool;
        field_value.val.boolean = true;
        pushJsonbValue(&state, WJB_VALUE, &field_value);

        field_key.type = jbvString;
        field_key.val.string.val = "test2";
        field_key.val.string.len = strlen("test2");
        res = pushJsonbValue(&state, WJB_KEY, &field_key);

        field_value.type = jbvBool;
        field_value.val.boolean = true;
        pushJsonbValue(&state, WJB_VALUE, &field_value);

        res = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
    }

    /*
     * The transition type for array_agg() is declared to be "internal", which
     * is a pass-by-value type the same size as a pointer.  So we can safely
     * pass the ArrayBuildState pointer through nodeAgg.c's machinations.
     */
    PG_RETURN_POINTER(state);
}

Datum
jsonb_agg_finalfn(PG_FUNCTION_ARGS)
{
    JsonbParseState     *state;
    JsonbValue          *res = NULL;

    /* cannot be called directly because of internal-type argument */
    Assert(AggCheckCallContext(fcinfo, NULL));

    state = PG_ARGISNULL(0) ? NULL : (JsonbParseState*) PG_GETARG_POINTER(0);

    if (state == NULL)
        PG_RETURN_NULL();

    res = pushJsonbValue(&state, WJB_END_ARRAY, NULL);
    PG_RETURN_POINTER(JsonbValueToJsonb(res));
}
