#include "postgres.h"

#include "catalog/pg_type.h"
#include "utils/jsonb.h"
#include "utils/builtins.h"

#include "jsonbx.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_pretty);
Datum jsonb_pretty(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_concat);
Datum jsonb_concat(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_delete);
Datum jsonb_delete(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_delete_idx);
Datum jsonb_delete_idx(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_delete_path);
Datum jsonb_delete_path(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(jsonb_set);
Datum jsonb_set(PG_FUNCTION_ARGS);

/*
 * jsonb_pretty:
 * Pretty-printed text for the jsonb
 */
Datum
jsonb_pretty(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB(0);
	StringInfo	str = makeStringInfo();

	JsonbToCStringWorker(str, &jb->root, VARSIZE(jb), true);

	PG_RETURN_TEXT_P(cstring_to_text_with_len(str->data, str->len));
}


/*
 * jsonb_concat:
 * Concatenation of two jsonb. There are few allowed combinations:
 * - concatenation of two objects
 * - concatenation of two arrays
 * - concatenation of object and array
 *
 * The result for first two is new object and array accordingly.
 * The last one return new array, which contains all elements from
 * original array, and one extra element (which is actually
 * other argument of this function with type jbvObject) at the first or last position.
 */
Datum
jsonb_concat(PG_FUNCTION_ARGS)
{
	Jsonb 				*jb1 = PG_GETARG_JSONB(0);
	Jsonb 				*jb2 = PG_GETARG_JSONB(1);
	Jsonb 				*out = palloc(VARSIZE(jb1) + VARSIZE(jb2));
	JsonbParseState 	*state = NULL;
	JsonbValue 			*res;
	JsonbIterator 		*it1, *it2;

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

	PG_RETURN_JSONB(out);
}


/*
 * jsonb_delete:
 * Return copy of jsonb with the specified item removed.
 * Item is a one key or element from jsonb, specified by name.
 * If there are many keys or elements with than name,
 * the first one will be removed.
 */
Datum
jsonb_delete(PG_FUNCTION_ARGS)
{
	Jsonb 				*in = PG_GETARG_JSONB(0);
	text 				*key = PG_GETARG_TEXT_PP(1);
	char 				*keyptr = VARDATA_ANY(key);
	int					keylen = VARSIZE_ANY_EXHDR(key);
	JsonbParseState 	*state = NULL;
	JsonbIterator 		*it;
	uint32 				r;
	JsonbValue 			v, *res = NULL;
	bool 				skipped = false;

	if (JB_ROOT_IS_SCALAR(in))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot get delete from scalar")));

	if (JB_ROOT_COUNT(in) == 0)
	{
		PG_RETURN_JSONB(in);
	}

	it = JsonbIteratorInit(&in->root);

	while((r = JsonbIteratorNext(&it, &v, false)) != 0)
	{
		if (!skipped && (r == WJB_ELEM || r == WJB_KEY) &&
			(v.type == jbvString && keylen == v.val.string.len &&
			 memcmp(keyptr, v.val.string.val, keylen) == 0))
		{
			/* we should delete only one key/element */
			skipped = true;

			if (r == WJB_KEY)
			{
				/* skip corresponding value */
				JsonbIteratorNext(&it, &v, true);
			}

			continue;
		}

		res = pushJsonbValue(&state, r, r < WJB_BEGIN_ARRAY ? &v : NULL);
	}

	Assert(res != NULL);
	PG_RETURN_JSONB(JsonbValueToJsonb(res));
}


/*
 * jsonb_delete_idx:
 * Return a copy of jsonb withour specified items.
 * Delete key (only from the top level of object) or element from jsonb by index (idx).
 * Negative idx value is supported, and it implies the countdown from the last key/element.
 * If idx is more, than numbers of keys/elements, or equal - nothing will be deleted.
 * If idx is negative and -idx is more, than number of keys/elements - the last one will be deleted.
 *
 * TODO: take care about nesting values.
 */
Datum
jsonb_delete_idx(PG_FUNCTION_ARGS)
{
	Jsonb 				*in = PG_GETARG_JSONB(0);
	int					idx = PG_GETARG_INT32(1);
	JsonbParseState 	*state = NULL;
	JsonbIterator 		*it;
	uint32 				r, i = 0, n;
	JsonbValue 			v, *res = NULL;

	if (JB_ROOT_IS_SCALAR(in))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot get delete from scalar")));

	if (JB_ROOT_COUNT(in) == 0)
	{
		PG_RETURN_JSONB(in);
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
		PG_RETURN_JSONB(in);
	}

	pushJsonbValue(&state, r, r < WJB_BEGIN_ARRAY ? &v : NULL);

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

		res = pushJsonbValue(&state, r, r < WJB_BEGIN_ARRAY ? &v : NULL);
	}

	Assert (res != NULL);
	PG_RETURN_JSONB(JsonbValueToJsonb(res));
}

/*
 * jsonb_set:
 * Replace/create value of jsonb key or jsonb element, which can be found by the specified path.
 * Path must be replesented as an array of key names or indexes. If indexes will be used,
 * the same rules implied as for jsonb_delete_idx (negative indexing and edge cases)
 */
Datum
jsonb_set(PG_FUNCTION_ARGS)
{
	Jsonb 				*in = PG_GETARG_JSONB(0);
	ArrayType 			*path = PG_GETARG_ARRAYTYPE_P(1);
	Jsonb 				*newval = PG_GETARG_JSONB(2);
	bool       			create = PG_GETARG_BOOL(3);
	JsonbValue 			*res = NULL;
	Datum 				*path_elems;
	bool 				*path_nulls;
	int					path_len;
	JsonbIterator 		*it;
	JsonbParseState 	*st = NULL;


	if (ARR_NDIM(path) > 1)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("wrong number of array subscripts")));

	if (JB_ROOT_IS_SCALAR(in))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot set path in scalar")));


	if (JB_ROOT_COUNT(in) == 0 && !create)
	{
		PG_RETURN_JSONB(in);
	}

	deconstruct_array(path, TEXTOID, -1, false, 'i',
					  &path_elems, &path_nulls, &path_len);

	if (path_len == 0)
	{
		PG_RETURN_JSONB(in);
	}

	it = JsonbIteratorInit(&in->root);

	res = setPath(&it, path_elems, path_nulls, path_len, &st, 0, newval, create);

	Assert (res != NULL);
	PG_RETURN_JSONB(JsonbValueToJsonb(res));
}


/*
 * jsonb_delete_path:
 */
Datum
jsonb_delete_path(PG_FUNCTION_ARGS)
{
	Jsonb	   *in = PG_GETARG_JSONB(0);
	ArrayType  *path = PG_GETARG_ARRAYTYPE_P(1);
	JsonbValue *res = NULL;
	Datum	   *path_elems;
	bool	   *path_nulls;
	int			path_len;
	JsonbIterator *it;
	JsonbParseState *st = NULL;

	if (ARR_NDIM(path) > 1)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("wrong number of array subscripts")));

	if (JB_ROOT_IS_SCALAR(in))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot delete path in scalar")));

	if (JB_ROOT_COUNT(in) == 0)
	{
		PG_RETURN_JSONB(in);
	}

	deconstruct_array(path, TEXTOID, -1, false, 'i',
					  &path_elems, &path_nulls, &path_len);

	if (path_len == 0)
	{
		PG_RETURN_JSONB(in);
	}

	it = JsonbIteratorInit(&in->root);

	res = setPath(&it, path_elems, path_nulls, path_len, &st, 0, NULL, false);

	Assert (res != NULL);
	PG_RETURN_JSONB(JsonbValueToJsonb(res));
}
