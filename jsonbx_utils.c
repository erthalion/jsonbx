#include "postgres.h"

#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"

#include "jsonbx.h"

#define choice_array(flag_val, a, b) flag_val == WJB_BEGIN_ARRAY ? a : b
#define choice_object(flag_val, a, b) flag_val == WJB_BEGIN_OBJECT ? a : b

#define is_array(flag_val, it) flag_val == WJB_BEGIN_ARRAY && !(*it)->isScalar

typedef bool (*walk_condition)(JsonbParseState**, JsonbValue*, uint32 /* token */, uint32 /* level */);
void printCR(StringInfo out, bool pretty_print);
void printIndent(StringInfo out, bool pretty_print, int level);
void jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal);
bool h_atoi(char *c, int l, int *acc);
JsonbValue* walkJsonb(JsonbIterator **it, JsonbValue *v, JsonbParseState **state, walk_condition);
bool untilLast(JsonbParseState **state, JsonbValue *v, uint32 token, uint32 level);


/*
 * JsonbToCStringExtended
 *	   Converts jsonb value to a C-string.
 * See the original function JsonbToCString in the jsonb.c
 */
char *
JsonbToCStringExtended(StringInfo out, JsonbContainer *in, int estimated_len, bool pretty_print)
{
	bool            first = true;
	JsonbIterator   *it;
	int             type = 0;
	JsonbValue      v;
	int             level = 0;
	bool            redo_switch = false;

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
					appendBinaryStringInfo(out, ", ", 2);
				}
				first = true;

				if (!v.val.array.rawScalar)
				{
					printCR(out, pretty_print);
					printIndent(out, pretty_print, level);
					appendStringInfoChar(out, '[');
				}
				level++;

				printCR(out, pretty_print);
				printIndent(out, pretty_print, level);

				break;
			case WJB_BEGIN_OBJECT:
				if (!first)
					appendBinaryStringInfo(out, ", ", 2);
				first = true;

				printCR(out, pretty_print);
				printIndent(out, pretty_print, level);

				appendStringInfoCharMacro(out, '{');

				level++;
				break;
			case WJB_KEY:
				if (!first)
					appendBinaryStringInfo(out, ", ", 2);
				first = true;

				printCR(out, pretty_print);
				printIndent(out, pretty_print, level);

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
					appendBinaryStringInfo(out, ", ", 2);

					printCR(out, pretty_print);
					printIndent(out, pretty_print, level);
				}
				else
					first = false;

				jsonb_put_escaped_value(out, &v);
				break;
			case WJB_END_ARRAY:
				level--;

				printCR(out, pretty_print);
				printIndent(out, pretty_print, level);

				if (!v.val.array.rawScalar)
					appendStringInfoChar(out, ']');
				first = false;
				break;
			case WJB_END_OBJECT:
				level--;

				printCR(out, pretty_print);
				printIndent(out, pretty_print, level);

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


void
printCR(StringInfo out, bool pretty_print)
{
	if (pretty_print)
	{
		appendBinaryStringInfo(out, "    ", 4);
		appendStringInfoCharMacro(out, '\n');
	}
}

void
printIndent(StringInfo out, bool pretty_print, int level)
{
	if (pretty_print)
	{
		int i;
		for(i=0; i<4*level; i++)
		{
			appendStringInfoCharMacro(out, ' ');
		}
	}
}


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

		/*
		 * Append the all tokens from v2 to res, include
		 * last WJB_END_OBJECT (the concatenation will be completed). 
		 */
		while((r2 = JsonbIteratorNext(it2, &v2, false)) != 0)
		{
			res = pushJsonbValue(state, r2, &v2);
		}
	}
	/*
	 * Both elements are arrays.
	 */
	else if (rk1 == WJB_BEGIN_ARRAY && rk2 == WJB_BEGIN_ARRAY)
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
				pushJsonbValue(state, WJB_ELEM, &v2);
			}
		}

		res = pushJsonbValue(state, WJB_END_ARRAY,
							  NULL/* signal to sort */);
	}
	/*
	 *  TODO: comments are needed
	 */
	else if ((is_array(rk1, it1) && rk2 == WJB_BEGIN_OBJECT) ||
			(rk1 == WJB_BEGIN_OBJECT && is_array(rk2, it2)))
	{
		JsonbIterator** it_array = choice_array(rk1, it1, it2);
		JsonbIterator** it_object = choice_object(rk1, it1, it2);

		JsonbValue* v_array = choice_array(rk1, &v1, &v2);
		JsonbValue* v_object = choice_object(rk1, &v1, &v2);

		bool prepend = (rk1 == WJB_BEGIN_OBJECT) ? true : false;

		pushJsonbValue(state, WJB_BEGIN_ARRAY, v_array);
		if (prepend)
		{
			pushJsonbValue(state, WJB_BEGIN_OBJECT, v_object);
			walkJsonb(it_object, v_object, state, NULL); 

			res = walkJsonb(it_array, v_array, state, NULL);
		}
		else
		{
			walkJsonb(it_array, v_array, state, untilLast);

			pushJsonbValue(state, WJB_BEGIN_OBJECT, v_object);
			walkJsonb(it_object, v_object, state, NULL); 

			res = pushJsonbValue(state, WJB_END_ARRAY, v_array);
		}
	}
	else
	{
		elog(ERROR, "invalid concatnation of jsonb objects");
	}

	return res;
}

/*
 * TODO: are there another examples of stop conditions?
 */
bool
untilLast(JsonbParseState **state, JsonbValue *v, uint32 token, uint32 level)
{
	return level == 0;
}

JsonbValue*
walkJsonb(JsonbIterator **it, JsonbValue *v, JsonbParseState **state, walk_condition until_condition)
{
	uint32          r, level = 1;
	JsonbValue      *res = NULL;

	while((r = JsonbIteratorNext(it, v, false)) != 0)
	{
		if (r == WJB_BEGIN_OBJECT || r == WJB_BEGIN_ARRAY) {
			++level;
		}
		else if (r == WJB_END_OBJECT || r == WJB_END_ARRAY) {
			--level;
		}

		if(until_condition != NULL && until_condition(state, v, r, level))
			break;

		res = pushJsonbValue(state, r, v);
	}

	return res;
}

JsonbValue*
replacePath(JsonbIterator **it, Datum *path_elems,
			  bool *path_nulls, int path_len,
			  JsonbParseState  **st, int level, JsonbValue *newval)
{
	JsonbValue  v, *res = NULL;
	int         r;

	r = JsonbIteratorNext(it, &v, false);

	if (r == WJB_BEGIN_ARRAY)
	{
		int     idx, i;
		uint32  n = v.val.array.nElems;

		idx = n;
		if (level >= path_len || path_nulls[level] ||
			h_atoi(VARDATA_ANY(path_elems[level]),
				   VARSIZE_ANY_EXHDR(path_elems[level]), &idx) == false)
		{
			idx = n;
		}
		else if (idx < 0)
		{
			if (-idx > n)
				idx = n;
			else
				idx = n + idx;
		}

		if (idx > n)
			idx = n;

		pushJsonbValue(st, r, &v);

		for(i=0; i<n; i++)
		{
			if (i == idx && level < path_len)
			{
				if (level == path_len - 1)
				{
					r = JsonbIteratorNext(it, &v, true); /* skip */
					Assert(r == WJB_ELEM);
					res = pushJsonbValue(st, r, newval);
				}
				else
				{
					res = replacePath(it, path_elems, path_nulls, path_len,
										st, level + 1, newval);
				}
			}
			else
			{
				r = JsonbIteratorNext(it, &v, false);
				Assert(r == WJB_ELEM);
				res = pushJsonbValue(st, r, &v);

				if (r == WJB_BEGIN_ARRAY || r == WJB_BEGIN_OBJECT)
				{
					int walking_level = 1;
					while(walking_level != 0)
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
						res = pushJsonbValue(st, r, &v);
					}
				}

			}
		}

		r = JsonbIteratorNext(it, &v, false);
		Assert(r == WJB_END_ARRAY);
		res = pushJsonbValue(st, r, &v);
	}
	else if (r == WJB_BEGIN_OBJECT)
	{
		int         i;
		uint32      n = v.val.object.nPairs;
		JsonbValue  k;
		bool        done = false;

		pushJsonbValue(st, WJB_BEGIN_OBJECT, &v);

		if (level >= path_len || path_nulls[level])
			done = true;

		for(i=0; i<n; i++)
		{
			r = JsonbIteratorNext(it, &k, true);
			Assert(r == WJB_KEY);
			res = pushJsonbValue(st, r, &k);

			if (done == false &&
				k.val.string.len == VARSIZE_ANY_EXHDR(path_elems[level]) &&
				memcmp(k.val.string.val, VARDATA_ANY(path_elems[level]),
					   k.val.string.len) == 0)
			{
				if (level == path_len - 1)
				{
					r = JsonbIteratorNext(it, &v, true); /* skip */
					Assert(r == WJB_VALUE);
					res = pushJsonbValue(st, r, newval);
				}
				else
				{
					res = replacePath(it, path_elems, path_nulls, path_len,
										st, level + 1, newval);
				}
			}
			else
			{
				r = JsonbIteratorNext(it, &v, false);
				Assert(r == WJB_VALUE);
				res = pushJsonbValue(st, r, &v);
				if (r == WJB_BEGIN_ARRAY || r == WJB_BEGIN_OBJECT)
				{
					int walking_level = 1;
					while(walking_level != 0)
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
						res = pushJsonbValue(st, r, &v);
					}
				}
			}
		}

		r = JsonbIteratorNext(it, &v, true);
		Assert(r == WJB_END_OBJECT);
		res = pushJsonbValue(st, r, &v);
	}
	else if (r == WJB_ELEM || r == WJB_VALUE)
	{
		pushJsonbValue(st, r, &v);
		res = (void*)0x01; /* dummy value */
	}
	else
	{
		elog(PANIC, "impossible state");
	}

	return res;
}

bool
h_atoi(char *c, int l, int *acc)
{
	bool    negative = false;
	char    *p = c;

	*acc = 0;

	while(isspace(*p) && p - c < l)
		p++;

	if (p - c >= l)
		return false;

	if (*p == '-')
	{
		negative = true;
		p++;
	}
	else if (*p == '+')
	{
		p++;
	}

	if (p - c >= l)
		return false;


	while(p - c < l)
	{
		if (!isdigit(*p))
			return false;

		*acc *= 10;
		*acc += (*p - '0');
		p++;
	}

	if (negative)
		*acc = - *acc;

	return true;
}
