-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION jsonbx" to load this file. \quit


CREATE FUNCTION jsonb_print(jsonb, pretty_print bool DEFAULT false)
RETURNS text
AS 'MODULE_PATHNAME', 'jsonb_print'
LANGUAGE C STRICT;
