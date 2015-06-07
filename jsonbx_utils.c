#include "postgres.h"

#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"

#include "jsonbx.h"

#define choice_array(flag_val, a, b) flag_val == WJB_BEGIN_ARRAY ? a : b
#define choice_object(flag_val, a, b) flag_val == WJB_BEGIN_OBJECT ? a : b

#define is_array(flag_val, it) flag_val == WJB_BEGIN_ARRAY && !(*it)->isScalar

typedef bool (*walk_condition)(JsonbParseState**, JsonbValue*, uint32 /* token */, uint32 /* level */);
void add_indent(StringInfo out, bool indent, int level);
void jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal);
bool h_atoi(char *c, int *acc);
JsonbValue *walkJsonb(JsonbIterator **it, JsonbParseState **state, bool stop_at_level_zero);
bool untilLast(JsonbParseState **state, JsonbValue *v, uint32 token, uint32 level);
void addJsonbToParseState(JsonbParseState **jbps, Jsonb * jb);

static void setPathObject(JsonbIterator **it, Datum *path_elems, bool *path_nulls,
							  int path_len, JsonbParseState **st, int level,
							  Jsonb *newval, uint32	nelems, bool create);
static void setPathArray(JsonbIterator **it, Datum *path_elems, bool *path_nulls,
							 int path_len, JsonbParseState **st, int level,
							 Jsonb *newval, uint32 npairs, bool create);



/*
 * JsonbToCStringExtended:
 * Convert jsonb value to a C-string taking into account "pretty_print" mode.
 * See the original function JsonbToCString in the jsonb.c
 * Only one considerable change is printCR and printIndent functions,
 * which add required line breaks and spaces accordingly to the nesting level.
 */
char *
JsonbToCStringWorker(StringInfo out, JsonbContainer *in, int estimated_len, bool indent)
{
	bool            first = true;
	JsonbIterator   *it;
	int             type = 0;
	JsonbValue      v;
	int             level = 0;
	bool            redo_switch = false;
	/* If we are indenting, don't add a space after a comma */
	int			ispaces = indent ? 1 : 2;
	/*
	 * Don't indent the very first item. This gets set to the indent flag
	 * at the bottom of the loop.
	 */
	bool        use_indent = false;
	bool        raw_scalar = false;

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
				{
					appendBinaryStringInfo(out, ", ", ispaces);
				}
				first = true;

				if (!v.val.array.rawScalar)
				{
					add_indent(out, use_indent, level);
					appendStringInfoCharMacro(out, '[');
				}
				else
				{
					raw_scalar = true;
				}
				level++;
				break;
			case WJB_BEGIN_OBJECT:
				if (!first)
					appendBinaryStringInfo(out, ", ", ispaces);
				first = true;

				add_indent(out, use_indent, level);
				appendStringInfoCharMacro(out, '{');

				level++;
				break;
			case WJB_KEY:
				if (!first)
					appendBinaryStringInfo(out, ", ", ispaces);
				first = true;

				add_indent(out, use_indent, level);

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
				if (!first)
				{
					appendBinaryStringInfo(out, ", ", ispaces);
				}

				first = false;

				if (!raw_scalar)
				{
					add_indent(out, use_indent, level);
				}

				jsonb_put_escaped_value(out, &v);
				break;
			case WJB_END_ARRAY:
				level--;

				if (!raw_scalar)
				{
					add_indent(out, use_indent, level);
					appendStringInfoChar(out, ']');
				}
				first = false;
				break;
			case WJB_END_OBJECT:
				level--;

				add_indent(out, use_indent, level);
				appendStringInfoCharMacro(out, '}');
				first = false;
				break;
			default:
				elog(ERROR, "unknown flag of jsonb iterator");
		}
		use_indent = indent;
	}

	Assert(level == 0);

	return out->data;
}


void
add_indent(StringInfo out, bool indent, int level)
{
	if (indent)
	{
		int			i;

		appendStringInfoCharMacro(out, '\n');
		for (i = 0; i < level; i++)
		{
			appendBinaryStringInfo(out, "    ", 4);
		}
	}
}


/*
 * jsonb_put_escaped_value:
 * Return string representation of jsonb value.
 */
void
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


/*
 * Iterate over all jsonb objects and merge them into one.
 * The logic of this function copied from the same hstore function,
 * except the case, when it1 & it2 represents jbvObject.
 * In that case we just append the content of it2 to it1 without any
 * verifications.
 */
JsonbValue *
IteratorConcat(JsonbIterator **it1, JsonbIterator **it2,
		JsonbParseState **state)
{
	uint32          r1, r2, rk1, rk2;
	JsonbValue      v1, v2, *res = NULL;

	r1 = rk1 = JsonbIteratorNext(it1, &v1, false);
	r2 = rk2 = JsonbIteratorNext(it2, &v2, false);

	/*
	 * Both elements are objects.
	 */
	if (rk1 == WJB_BEGIN_OBJECT && rk2 == WJB_BEGIN_OBJECT)
	{
		int         level = 1;

		/*
		 * Append the all tokens from v1 to res, exept
		 * last WJB_END_OBJECT (because res will not be finished yet).
		 */
		(void) pushJsonbValue(state, r1, NULL);
		while((r1 = JsonbIteratorNext(it1, &v1, false)) != 0)
		{
			if (r1 == WJB_BEGIN_OBJECT) {
				++level;
			}
			else if (r1 == WJB_END_OBJECT) {
				--level;
			}

			if (level != 0) {
				res = pushJsonbValue(state, r1, r1 < WJB_BEGIN_ARRAY ? &v1 : NULL);
			}
		}

		/*
		 * Append the all tokens from v2 to res, include
		 * last WJB_END_OBJECT (the concatenation will be completed). 
		 */
		while((r2 = JsonbIteratorNext(it2, &v2, false)) != 0)
		{
			res = pushJsonbValue(state, r2, r2 < WJB_BEGIN_ARRAY ? &v2 : NULL);
		}
	}
	/*
	 * Both elements are arrays (either can be scalar).
	 */
	else if (rk1 == WJB_BEGIN_ARRAY && rk2 == WJB_BEGIN_ARRAY)
	{
		res = pushJsonbValue(state, r1, NULL);
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
					pushJsonbValue(state, WJB_KEY, NULL);
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
							 NULL /* signal to sort */ );
	}
	/*
	 *  One of the elements is object, another is array.
	 *	There is only one reasonable approach to handle this - 
	 *	include object into array as one of the elements.
	 *	Position of this new element will depends on the arguments
	 *	order:
	 *	- if the first argument is object, then it will be the first element in array
	 *	- if the second argument is object, then it will be the last element in array
	 */
	else if ((is_array(rk1, it1) && rk2 == WJB_BEGIN_OBJECT) ||
			(rk1 == WJB_BEGIN_OBJECT && is_array(rk2, it2)))
	{
		JsonbIterator** it_array = choice_array(rk1, it1, it2);
		JsonbIterator** it_object = choice_object(rk1, it1, it2);

		bool prepend = (rk1 == WJB_BEGIN_OBJECT) ? true : false;

		pushJsonbValue(state, WJB_BEGIN_ARRAY, NULL);
		if (prepend)
		{
			pushJsonbValue(state, WJB_BEGIN_OBJECT, NULL);
			walkJsonb(it_object, state, false);

			res = walkJsonb(it_array, state, false);
		}
		else
		{
			walkJsonb(it_array, state, true);

			pushJsonbValue(state, WJB_BEGIN_OBJECT, NULL);
			walkJsonb(it_object, state, false);

			res = pushJsonbValue(state, WJB_END_ARRAY, NULL);
		}
	}
	else
	{
		elog(ERROR, "invalid concatnation of jsonb objects");
	}

	return res;
}

/*
 * One of possible conditions for walkJsonb.
 * This condition implies, that entire Jsonb should be converted.
 */
bool
untilLast(JsonbParseState **state, JsonbValue *v, uint32 token, uint32 level)
{
	return level == 0;
}


/*
 * walkJsonb:
 * Copy elements from the iterator to the parse state
 * stopping at level zero if required.
 */
JsonbValue*
walkJsonb(JsonbIterator **it, JsonbParseState **state, bool stop_at_level_zero)
{
	uint32          r, level = 1;
	JsonbValue      v, *res = NULL;

	while((r = JsonbIteratorNext(it, &v, false)) != WJB_DONE)
	{
		if (r == WJB_BEGIN_OBJECT || r == WJB_BEGIN_ARRAY) {
			++level;
		}
		else if (r == WJB_END_OBJECT || r == WJB_END_ARRAY) {
			--level;
		}

		if(stop_at_level_zero && level == 0)
			break;

		res = pushJsonbValue(state, r, r < WJB_BEGIN_ARRAY ? &v : NULL);
	}

	return res;
}

/*
 * setPath:
 * Recursive replacement/creation function for jsonb_set.
 * Replace/create value of jsonb key or jsonb element, which can be found by the specified path on the specific level.
 * For jbvArray level is the current element, for jbvObject is the current nesting level.
 * For each recursion step, level value will be incremented, and an array element or object key will be replaces or created,
 * if current level is path_len - 1 (it does mean, that we've reached the last element in the path).
 * If indexes will be used, the same rules implied as for jsonb_delete_idx (negative indexing and edge cases)
 */
JsonbValue*
setPath(JsonbIterator **it, Datum *path_elems,
			  bool *path_nulls, int path_len,
			  JsonbParseState  **st, int level, Jsonb *newval, bool create)
{
	JsonbValue  v, *res = NULL;
	int         r;

	r = JsonbIteratorNext(it, &v, false);

	switch (r)
	{
		case WJB_BEGIN_ARRAY:
			(void) pushJsonbValue(st, r, NULL);
			setPathArray(it, path_elems, path_nulls, path_len, st, level,
							 newval, v.val.array.nElems, create);
			r = JsonbIteratorNext(it, &v, false);
			Assert(r == WJB_END_ARRAY);
			res = pushJsonbValue(st, r, NULL);

			break;
		case WJB_BEGIN_OBJECT:
			(void) pushJsonbValue(st, r, NULL);
			setPathObject(it, path_elems, path_nulls, path_len, st, level,
							  newval, v.val.object.nPairs, create);
			r = JsonbIteratorNext(it, &v, true);
			Assert(r == WJB_END_OBJECT);
			res = pushJsonbValue(st, r, NULL);

			break;
		case WJB_ELEM:
		case WJB_VALUE:
			res = pushJsonbValue(st, r, &v);
			break;
		default:
			elog(PANIC, "impossible state");
	}

	return res;
}

/*
 * Object walker for setPath
 */
static void
setPathObject(JsonbIterator **it, Datum *path_elems, bool *path_nulls,
				  int path_len, JsonbParseState **st, int level,
				  Jsonb *newval, uint32	npairs, bool create)
{
	JsonbValue	v;
	int			i;
	JsonbValue	k;
	bool		done = false;

	if (level >= path_len || path_nulls[level])
		done = true;

	/* empty object is a special case for create */
	if ((npairs == 0) && create && (level == path_len - 1))
	{
		JsonbValue	newkey;

		newkey.type = jbvString;
		newkey.val.string.len = VARSIZE_ANY_EXHDR(path_elems[level]);
		newkey.val.string.val = VARDATA_ANY(path_elems[level]);

		(void) pushJsonbValue(st, WJB_KEY, &newkey);
		addJsonbToParseState(st, newval);
	}

	/* iterate over object keys */
	for (i = 0; i < npairs; i++)
	{
		int		r = JsonbIteratorNext(it, &k, true);
		Assert(r == WJB_KEY);

		if (!done &&
			k.val.string.len == VARSIZE_ANY_EXHDR(path_elems[level]) &&
			memcmp(k.val.string.val, VARDATA_ANY(path_elems[level]),
				   k.val.string.len) == 0)
		{
			/*
			 * The current path item was found.
			 * If we reached the end of path, current element will be replaced
			 * Otherwise level value will be incremented, and the next step of
			 * recursion will be started.
			 */
			if (level == path_len - 1)
			{
				r = JsonbIteratorNext(it, &v, true);		/* skip */
				if (newval != NULL)
				{
					(void) pushJsonbValue(st, WJB_KEY, &k);
					addJsonbToParseState(st, newval);
				}
				done = true;
			}
			else
			{
				(void) pushJsonbValue(st, r, &k);
				setPath(it, path_elems, path_nulls, path_len,
							st, level + 1, newval, create);
			}
		}
		else
		{
			if (create && !done && level == path_len - 1 && i == npairs - 1)
			{
				JsonbValue new = k;
				new.val.string.len = VARSIZE_ANY_EXHDR(path_elems[level]);
				new.val.string.val = VARDATA_ANY(path_elems[level]);

				(void) pushJsonbValue(st, WJB_KEY, &new);
				addJsonbToParseState(st, newval);
			}

			/* We are out of the specified path, skip the rest of elements */
			(void) pushJsonbValue(st, r, &k);
			r = JsonbIteratorNext(it, &v, false);

			(void) pushJsonbValue(st, r, r < WJB_BEGIN_ARRAY ? &v : NULL);
			if (r == WJB_BEGIN_ARRAY || r == WJB_BEGIN_OBJECT)
			{
				int		walking_level = 1;

				while (walking_level != 0)
				{
					r = JsonbIteratorNext(it, &v, false);

					if (r == WJB_BEGIN_ARRAY || r == WJB_BEGIN_OBJECT)
					{
						++walking_level;
					}
					if (r == WJB_END_ARRAY || r == WJB_END_OBJECT)
					{
						--walking_level;
					}

					(void) pushJsonbValue(st, r, r < WJB_BEGIN_ARRAY ? &v : NULL);
				}
			}
		}
	}
}

/*
 * Array walker for setPath
 */
static void
setPathArray(JsonbIterator **it, Datum *path_elems, bool *path_nulls,
				 int path_len, JsonbParseState **st, int level,
				 Jsonb *newval, uint32 nelems, bool create)
{
	JsonbValue	v;
	int			idx,
				i;
	char	   *badp;
	bool		done = false;

	/* If we can't convert path element to integer index,
	 * the last element will be used.
	 */
	if (level < path_len && !path_nulls[level])
	{
		char	   *c = VARDATA_ANY(path_elems[level]);

		errno = 0;
		idx = (int) strtol(c, &badp, 10);
		if (errno != 0 || badp == c)
			idx = nelems;
	}
	/* Otherwise we should take care about negative indexes,
	 * it implies the countdown from the last element.
	 * If -idx is more, than number of elements - the last element will be used
	 */
	else
	{
		idx = nelems;
	}

	if (idx < 0)
	{
		if (-idx > nelems)
			idx = -1;
		else
			idx = nelems + idx;
	}

	if (idx > 0 && idx > nelems)
		idx = nelems;

	/*
	 * if we're creating, and idx == -1, we prepend the new value to the array
	 * also if the array is empty - in which case we don't really care what the
	 * idx value is
	 */

	if ((idx == -1 || nelems == 0) && create && (level == path_len - 1))
	{
		Assert(newval != NULL);
		addJsonbToParseState(st, newval);
		done = true;
	}

	/* iterate over the array elements */
	for (i = 0; i < nelems; i++)
	{
		int		r;

		if (i == idx && level < path_len)
		{
			/*
			 * The current path item was found.
			 * If we reached the end of path, current element will be replaced
			 * Otherwise level value will be incremented, and the next step of
			 * recursion will be started.
			 */
			if (level == path_len - 1)
			{
				r = JsonbIteratorNext(it, &v, true);		/* skip */
				if (newval != NULL)
				{
					addJsonbToParseState(st, newval);
				}
				done = true;
			}
			else
				(void) setPath(it, path_elems, path_nulls, path_len,
								   st, level + 1, newval, create);
		}
		else
		{
			/* We are out of the specified path, skip the rest of elements */
			r = JsonbIteratorNext(it, &v, false);

			(void) pushJsonbValue(st, r, r < WJB_BEGIN_ARRAY ? &v : NULL);

			if (r == WJB_BEGIN_ARRAY || r == WJB_BEGIN_OBJECT)
			{
				int		walking_level = 1;

				while (walking_level != 0)
				{
					r = JsonbIteratorNext(it, &v, false);

					if (r == WJB_BEGIN_ARRAY || r == WJB_BEGIN_OBJECT)
					{
						++walking_level;
					}
					if (r == WJB_END_ARRAY || r == WJB_END_OBJECT)
					{
						--walking_level;
					}

					(void) pushJsonbValue(st, r, r < WJB_BEGIN_ARRAY ? &v : NULL);
				}
			}

			if (create && !done && level == path_len - 1 && i == nelems - 1)
			{
				addJsonbToParseState(st, newval);
			}

		}
	}
}


/*
 * Add values from the jsonb to the parse state.
 *
 * If the parse state container is an object, the jsonb is pushed as
 * a value, not a key.
 *
 * This needs to be done using an iterator because pushJsonbValue doesn't
 * like getting jbvBinary values, so we can't just push jb as a whole.
 */
void
addJsonbToParseState(JsonbParseState **jbps, Jsonb * jb)
{

	JsonbIterator	*it;
	JsonbValue		*o = &(*jbps)->contVal;
	int				type;
	JsonbValue		v;

	it = JsonbIteratorInit(&jb->root);

	Assert(o->type == jbvArray || o->type == jbvObject);

	if (JB_ROOT_IS_SCALAR(jb))
	{
		(void) JsonbIteratorNext(&it, &v, false); /* skip array header */
		(void) JsonbIteratorNext(&it, &v, false); /* fetch scalar value */

		switch (o->type)
		{
			case jbvArray:
				(void) pushJsonbValue(jbps, WJB_ELEM, &v);
				break;
			case jbvObject:
				(void) pushJsonbValue(jbps, WJB_VALUE, &v);
				break;
			default:
				elog(ERROR, "unexpected parent of nested structure");
		}
	}
	else
	{
		while ((type = JsonbIteratorNext(&it, &v, false)) != WJB_DONE)
		{
			if (type == WJB_KEY || type == WJB_VALUE || type == WJB_ELEM)
				(void) pushJsonbValue(jbps, type, &v);
			else
				(void) pushJsonbValue(jbps, type, NULL);
		}
	}
}
