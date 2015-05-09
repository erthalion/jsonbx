-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION jsonbx" to load this file. \quit


CREATE FUNCTION jsonb_pretty(jsonb)
RETURNS text
AS 'MODULE_PATHNAME', 'jsonb_pretty'
LANGUAGE C STRICT;

CREATE FUNCTION jsonb_concat(jsonb, jsonb)
RETURNS jsonb
AS 'MODULE_PATHNAME', 'jsonb_concat'
LANGUAGE C STRICT;

CREATE OPERATOR || (
	LEFTARG = jsonb,
	RIGHTARG = jsonb,
	PROCEDURE = jsonb_concat
);

CREATE FUNCTION jsonb_delete(jsonb,text)
RETURNS jsonb
AS 'MODULE_PATHNAME','jsonb_delete'
LANGUAGE C STRICT;

CREATE OPERATOR - (
	LEFTARG = jsonb,
	RIGHTARG = text,
	PROCEDURE = jsonb_delete
);

CREATE FUNCTION jsonb_delete(jsonb,int)
RETURNS jsonb
AS 'MODULE_PATHNAME','jsonb_delete_idx'
LANGUAGE C STRICT;

CREATE OPERATOR - (
	LEFTARG = jsonb,
	RIGHTARG = int,
	PROCEDURE = jsonb_delete
);

CREATE FUNCTION jsonb_delete(jsonb,text[])
RETURNS jsonb
AS 'MODULE_PATHNAME','jsonb_delete_path'
LANGUAGE C STRICT;

CREATE OPERATOR - (
	LEFTARG = jsonb,
	RIGHTARG = text[],
	PROCEDURE = jsonb_delete
);

CREATE FUNCTION jsonb_replace(jsonb,text[],jsonb)
RETURNS jsonb
AS 'MODULE_PATHNAME','jsonb_replace'
LANGUAGE C STRICT;
