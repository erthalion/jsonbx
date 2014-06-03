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

/*
 * State used while converting an arbitrary JsonbValue into a Jsonb value
 * (4-byte varlena uncompressed representation of a Jsonb)
 *
 * ConvertLevel:  Bookkeeping around particular level when converting.
 */
typedef struct convertLevel
{
	uint32		i;		/* Iterates once per element, or once per pair */
	uint32	   *header; /* Pointer to current container header */
	JEntry	   *meta;	/* This level's metadata */
	char	   *begin;	/* Pointer into convertState.buffer */
} convertLevel;

/*
 * convertState:  Overall bookkeeping state for conversion
 */
typedef struct convertState
{
	/* Preallocated buffer in which to form varlena/Jsonb value */
	Jsonb			   *buffer;
	/* Pointer into buffer */
	char			   *ptr;

	/* State for  */
	convertLevel	   *allState,	/* Overall state array */
					   *contPtr;	/* Cur container pointer (in allState) */

	/* Current size of buffer containing allState array */
	Size				levelSz;

}	convertState;


char * JsonbToCStringExtended(StringInfo out, JsonbContainer in, int estimated_len, JsonbOutputKind kind);
static void printCR(StringInfo out, JsonbOutputKind kind);
static void printIndent(StringInfo out, JsonbOutputKind kind, int level);
static void jsonb_put_escaped_value(StringInfo out, JsonbValue * scalarVal);

static JsonbValue * IteratorConcat(JsonbIterator **it1, JsonbIterator **it2, JsonbParseState **state);
static int lexicalCompareJsonbStringValue(const void *a, const void *b);

static Jsonb *convertToJsonb(JsonbValue *val);
static void convertJsonbValue(StringInfo buffer, JEntry *header, JsonbValue *val, int level);
static void convertJsonbArray(StringInfo buffer, JEntry *header, JsonbValue *val, int level);
static void convertJsonbObject(StringInfo buffer, JEntry *header, JsonbValue *val, int level);
static void convertJsonbScalar(StringInfo buffer, JEntry *header, JsonbValue *scalarVal);

static int reserveFromBuffer(StringInfo buffer, int len);
static void appendToBuffer(StringInfo buffer, const char *data, int len);
static void copyToBuffer(StringInfo buffer, int offset, const char *data, int len);
static short padBufferToInt(StringInfo buffer);


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
        uint32 r;

        if (res->type == jbvArray && res->val.array.nElems > 1)
            res->val.array.rawScalar = false;

        out = convertToJsonb(res);
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
        bool            fin2 = false,
                        keyIsDef = false;

        res = pushJsonbValue(state, r1, &v1);

        for(;;)
        {
            r1 = JsonbIteratorNext(it1, &v1, true);

            Assert(r1 == WJB_KEY || r1 == WJB_VALUE || r1 == WJB_END_OBJECT);

            if (r1 == WJB_KEY && fin2 == false)
            {
                int diff  = 1;

                if (keyIsDef)
                    r2 = WJB_KEY;

                while(keyIsDef || (r2 = JsonbIteratorNext(it2, &v2, true)) != 0)
                {
                    if (r2 != WJB_KEY)
                        continue;

                    /*diff = lengthCompareJsonbStringValue(&v1, &v2, NULL);*/
                    diff = lexicalCompareJsonbStringValue(&v1, &v2);

                    if (diff > 0)
                    {
                        if (keyIsDef)
                            keyIsDef = false;

                        pushJsonbValue(state, r2, &v2);
                        r2 = JsonbIteratorNext(it2, &v2, true);
                        Assert(r2 == WJB_VALUE);
                        pushJsonbValue(state, r2, &v2);
                    }
                    else if (diff <= 0)
                    {
                        break;
                    }
                }

                if (r2 == 0)
                {
                    fin2 = true;
                }
                else if (diff == 0)
                {
                    keyIsDef = false;

                    pushJsonbValue(state, r1, &v1);

                    r1 = JsonbIteratorNext(it1, &v1, true); // ignore
                    r2 = JsonbIteratorNext(it2, &v2, true); // new val

                    Assert(r1 == WJB_VALUE && r2 == WJB_VALUE);
                    pushJsonbValue(state, r2, &v2);

                    continue;
                }
                else
                {
                    keyIsDef = true;
                }
            }
            else if (r1 == WJB_END_OBJECT)
            {
                if (r2 != 0)
                {
                    if (keyIsDef)
                        r2 = WJB_KEY;

                    while(keyIsDef ||
                          (r2 = JsonbIteratorNext(it2, &v2, true)) != 0)
                    {
                        if (r2 != WJB_KEY)
                            continue;

                        pushJsonbValue(state, r2, &v2);
                        r2 = JsonbIteratorNext(it2, &v2, true);
                        Assert(r2 == WJB_VALUE);
                        pushJsonbValue(state, r2, &v2);
                        keyIsDef = false;
                    }
                }

                res = pushJsonbValue(state, r1, &v1);
                break;
            }

            res = pushJsonbValue(state, r1, &v1);
        }
    }
    else if ((rk1 == WJB_BEGIN_OBJECT || rk1 == WJB_BEGIN_ARRAY) &&
             (rk2 == WJB_BEGIN_OBJECT || rk2 == WJB_BEGIN_ARRAY))
    {
        if (rk1 == WJB_BEGIN_OBJECT && rk2 == WJB_BEGIN_ARRAY &&
            v2.val.array.nElems % 2 != 0)
            elog(ERROR, "hstore's array must have even number of elements");

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
                    /*convertScalarToString(&v2);*/
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


/*
 * Standard lexical qsort() comparator of jsonb strings.
 *
 * Sorts strings lexically, using the default database collation.  Used by
 * B-Tree operators, where a lexical sort order is generally expected.
 */
static int
lexicalCompareJsonbStringValue(const void *a, const void *b)
{
	const JsonbValue *va = (const JsonbValue *) a;
	const JsonbValue *vb = (const JsonbValue *) b;

	Assert(va->type == jbvString);
	Assert(vb->type == jbvString);

	return varstr_cmp(va->val.string.val, va->val.string.len, vb->val.string.val,
					  vb->val.string.len, DEFAULT_COLLATION_OID);
}


/*
 * Given a JsonbValue, convert to Jsonb. The result is palloc'd.
 */
static Jsonb *
convertToJsonb(JsonbValue *val)
{
	StringInfoData buffer;
	JEntry		jentry;
	Jsonb	   *res;

	/* Should not already have binary representation */
	Assert(val->type != jbvBinary);

	/* Allocate an output buffer. It will be enlarged as needed */
	initStringInfo(&buffer);

	/* Make room for the varlena header */
	reserveFromBuffer(&buffer, sizeof(VARHDRSZ));

	convertJsonbValue(&buffer, &jentry, val, 0);

	/*
	 * Note: the JEntry of the root is discarded. Therefore the root
	 * JsonbContainer struct must contain enough information to tell what
	 * kind of value it is.
	 */

	res = (Jsonb *) buffer.data;

	SET_VARSIZE(res, buffer.len);

	return res;
}

/*
 * Subroutine of convertJsonb: serialize a single JsonbValue into buffer.
 *
 * The JEntry header for this node is returned in *header. It is filled in
 * with the length of this value, but if it is stored in an array or an
 * object (which is always, except for the root node), it is the caller's
 * responsibility to adjust it with the offset within the container.
 *
 * If the value is an array or an object, this recurses. 'level' is only used
 * for debugging purposes.
 */
static void
convertJsonbValue(StringInfo buffer, JEntry *header, JsonbValue *val, int level)
{
	check_stack_depth();

	if (!val)
		return;

	if (IsAJsonbScalar(val) || val->type == jbvBinary)
		convertJsonbScalar(buffer, header, val);
	else if (val->type == jbvArray)
		convertJsonbArray(buffer, header, val, level);
	else if (val->type == jbvObject)
		convertJsonbObject(buffer, header, val, level);
	else
		elog(ERROR, "unknown type of jsonb container");
}

static void
convertJsonbArray(StringInfo buffer, JEntry *pheader, JsonbValue *val, int level)
{
	int			offset;
	int			metaoffset;
	int			i;
	int			totallen;
	uint32		header;

	/* Initialize pointer into conversion buffer at this level */
	offset = buffer->len;

	padBufferToInt(buffer);

	/*
	 * Construct the header Jentry, stored in the beginning of the variable-
	 * length payload.
	 */
	header = val->val.array.nElems | JB_FARRAY;
	if (val->val.array.rawScalar)
	{
		Assert(val->val.array.nElems == 1);
		Assert(level == 0);
		header |= JB_FSCALAR;
	}

	appendToBuffer(buffer, (char *) &header, sizeof(uint32));
	/* reserve space for the JEntries of the elements. */
	metaoffset = reserveFromBuffer(buffer, sizeof(JEntry) * val->val.array.nElems);

	totallen = 0;
	for (i = 0; i < val->val.array.nElems; i++)
	{
		JsonbValue *elem = &val->val.array.elems[i];
		int			len;
		JEntry		meta;

		convertJsonbValue(buffer, &meta, elem, level + 1);
		len = meta & JENTRY_POSMASK;
		totallen += len;

		if (totallen > JENTRY_POSMASK)
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("total size of jsonb array elements exceeds the maximum of %u bytes",
							JENTRY_POSMASK)));

		if (i > 0)
			meta = (meta & ~JENTRY_POSMASK) | totallen;
		copyToBuffer(buffer, metaoffset, (char *) &meta, sizeof(JEntry));
		metaoffset += sizeof(JEntry);
	}

	totallen = buffer->len - offset;

	/* Initialize the header of this node, in the container's JEntry array */
	*pheader = JENTRY_ISCONTAINER | totallen;
}

static void
convertJsonbObject(StringInfo buffer, JEntry *pheader, JsonbValue *val, int level)
{
	uint32		header;
	int			offset;
	int			metaoffset;
	int			i;
	int			totallen;

	/* Initialize pointer into conversion buffer at this level */
	offset = buffer->len;

	padBufferToInt(buffer);

	/* Initialize header */
	header = val->val.object.nPairs | JB_FOBJECT;
	appendToBuffer(buffer, (char *) &header, sizeof(uint32));

	/* reserve space for the JEntries of the keys and values */
	metaoffset = reserveFromBuffer(buffer, sizeof(JEntry) * val->val.object.nPairs * 2);

	totallen = 0;
	for (i = 0; i < val->val.object.nPairs; i++)
	{
		JsonbPair *pair = &val->val.object.pairs[i];
		int len;
		JEntry meta;

		/* put key */
		convertJsonbScalar(buffer, &meta, &pair->key);

		len = meta & JENTRY_POSMASK;
		totallen += len;

		if (totallen > JENTRY_POSMASK)
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("total size of jsonb array elements exceeds the maximum of %u bytes",
							JENTRY_POSMASK)));

		if (i > 0)
			meta = (meta & ~JENTRY_POSMASK) | totallen;
		copyToBuffer(buffer, metaoffset, (char *) &meta, sizeof(JEntry));
		metaoffset += sizeof(JEntry);

		convertJsonbValue(buffer, &meta, &pair->value, level);
		len = meta & JENTRY_POSMASK;
		totallen += len;

		if (totallen > JENTRY_POSMASK)
			ereport(ERROR,
					(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
					 errmsg("total size of jsonb array elements exceeds the maximum of %u bytes",
							JENTRY_POSMASK)));

		meta = (meta & ~JENTRY_POSMASK) | totallen;
		copyToBuffer(buffer, metaoffset, (char *) &meta, sizeof(JEntry));
		metaoffset += sizeof(JEntry);
	}

	totallen = buffer->len - offset;

	*pheader = JENTRY_ISCONTAINER | totallen;
}

static void
convertJsonbScalar(StringInfo buffer, JEntry *jentry, JsonbValue *scalarVal)
{
	int			numlen;
	short		padlen;

	switch (scalarVal->type)
	{
		case jbvNull:
			*jentry = JENTRY_ISNULL;
			break;

		case jbvString:
			appendToBuffer(buffer, scalarVal->val.string.val, scalarVal->val.string.len);

			*jentry = scalarVal->val.string.len;
			break;

		case jbvNumeric:
			numlen = VARSIZE_ANY(scalarVal->val.numeric);
			padlen = padBufferToInt(buffer);

			appendToBuffer(buffer, (char *) scalarVal->val.numeric, numlen);

			*jentry = JENTRY_ISNUMERIC | (padlen + numlen);
			break;

		case jbvBool:
			*jentry = (scalarVal->val.boolean) ?
				JENTRY_ISBOOL_TRUE : JENTRY_ISBOOL_FALSE;
			break;

		default:
			elog(ERROR, "invalid jsonb scalar type");
	}
}


/*
 * Reserve 'len' bytes, at the end of the buffer, enlarging it if necessary.
 * Returns the offset to the reserved area. The caller is expected to fill
 * the reserved area later with copyToBuffer().
 */
static int
reserveFromBuffer(StringInfo buffer, int len)
{
	int			offset;

	/* Make more room if needed */
	enlargeStringInfo(buffer, len);

	/* remember current offset */
	offset = buffer->len;

	/* reserve the space */
	buffer->len += len;

	/*
	 * Keep a trailing null in place, even though it's not useful for us;
	 * it seems best to preserve the invariants of StringInfos.
	 */
	buffer->data[buffer->len] = '\0';

	return offset;
}

/*
 * Copy 'len' bytes to a previously reserved area in buffer.
 */
static void
copyToBuffer(StringInfo buffer, int offset, const char *data, int len)
{
	memcpy(buffer->data + offset, data, len);
}

/*
 * A shorthand for reserveFromBuffer + copyToBuffer.
 */
static void
appendToBuffer(StringInfo buffer, const char *data, int len)
{
	int			offset;

	offset = reserveFromBuffer(buffer, len);
	copyToBuffer(buffer, offset, data, len);
}


/*
 * Append padding, so that the length of the StringInfo is int-aligned.
 * Returns the number of padding bytes appended.
 */
static short
padBufferToInt(StringInfo buffer)
{
	int			padlen,
				p,
				offset;

	padlen = INTALIGN(buffer->len) - buffer->len;

	offset = reserveFromBuffer(buffer, padlen);

	/* padlen must be small, so this is probably faster than a memset */
	for (p = 0; p < padlen; p++)
		buffer->data[offset + p] = '\0';

	return padlen;
}
