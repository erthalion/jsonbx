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

/*
 * jsonb_print:
 * Allows jsonb displaying in two modes - regular and "prettified".
 * Boolean flag "pretty_print" defines which mode will be used
 * "prettified" (true) or regular (false).
 */
Datum
jsonb_print(PG_FUNCTION_ARGS)
{
	Jsonb 			*jb = PG_GETARG_JSONB(0);
	text 			*out;
	int				pretty_print = 0;
	StringInfo 		str = makeStringInfo();

	if (PG_GETARG_BOOL(1))
		pretty_print = 1;

	appendBinaryStringInfo(str, "	", 4); /* VARHDRSZ */
	JsonbToCStringExtended(str, &jb->root, VARSIZE(jb), pretty_print);

	out = (text*)str->data;
	SET_VARSIZE(out, str->len);

	PG_RETURN_TEXT_P(out);
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

	PG_RETURN_POINTER(out);
}


/*
 * jsonb_delete:
 * Delete one key or element from jsonb by name.
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
	Jsonb 				*out = palloc(VARSIZE(in));
	JsonbParseState 	*state = NULL;
	JsonbIterator 		*it;
	uint32 				r;
	JsonbValue 			v, *res = NULL;
	bool 				skipped = false;

	SET_VARSIZE(out, VARSIZE(in));

	if (JB_ROOT_COUNT(in) == 0)
	{
		PG_RETURN_POINTER(out);
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


/*
 * jsonb_delete_idx:
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
	Jsonb 				*out = palloc(VARSIZE(in));
	JsonbParseState 	*state = NULL;
	JsonbIterator 		*it;
	uint32 				r, i = 0, n;
	JsonbValue 			v, *res = NULL;

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

/*
 * jsonb_replace:
 * Replace value of jsonb key or jsonb element, which can be found by the specified path.
 * Path must be replesented as an array of key names or indexes. If indexes will be used,
 * the same rules implied as for jsonb_delete_idx (negative indexing and edge cases)
 */
Datum
jsonb_replace(PG_FUNCTION_ARGS)
{
	Jsonb 				*in = PG_GETARG_JSONB(0);
	ArrayType 			*path = PG_GETARG_ARRAYTYPE_P(1);
	Jsonb 				*newval = PG_GETARG_JSONB(2);
	Jsonb 				*out = palloc(VARSIZE(in) + VARSIZE(newval));
	JsonbValue 			value, *res = NULL;
	Datum 				*path_elems;
	bool 				*path_nulls;
	int					path_len;
	JsonbIterator 		*it;
	JsonbParseState 	*st = NULL;

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

		/*
		 * New value can have a complex type (object or array).
		 * In that case we must unwrap this value to JsonbValue.
		 */
		while((r = JsonbIteratorNext(&it, &value, false)) != 0)
		{
			res = pushJsonbValue(&st, r, &value);
		}
		
		/*
		 * if new value is a scalar,
		 * we must extract this scalar from jbvArray
		 */
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
