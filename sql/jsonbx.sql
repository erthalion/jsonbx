CREATE EXTENSION jsonbx;

select jsonb_print('{"a": "test", "b": [1, 2, 3], "c": "test3", "d":{"dd": "test4", "dd2":{"ddd": "test5"}}}'::jsonb, true);

select jsonb_concat('{"d": "test", "a": [1, 2]}'::jsonb, '{"g": "test2", "c": {"c1":1, "c2":2}}'::jsonb);

select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"cq":"l", "b":"g", "fg":false}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"aq":"l"}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"aa":"l"}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{}';
select '{}'::jsonb || '{"cq":"l", "b":"g", "fg":false}';
select pg_column_size('{}'::jsonb || '{}'::jsonb) = pg_column_size('{}'::jsonb);
select pg_column_size('{"aa":1}'::jsonb || '{"b":2}'::jsonb) = pg_column_size('{"aa":1, "b":2}'::jsonb);
select pg_column_size('{"aa":1, "b":2}'::jsonb || '{}'::jsonb) = pg_column_size('{"aa":1, "b":2}'::jsonb);
select pg_column_size('{}'::jsonb || '{"aa":1, "b":2}'::jsonb) = pg_column_size('{"aa":1, "b":2}'::jsonb);

select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'a');
select jsonb_delete('{"a":null , "b":2, "c":3}'::jsonb, 'a');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'b');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'c');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'd');
select '{"a":1 , "b":2, "c":3}'::jsonb - 'a'::text;
select '{"a":null , "b":2, "c":3}'::jsonb - 'a'::text;
select '{"a":1 , "b":2, "c":3}'::jsonb - 'b'::text;
select '{"a":1 , "b":2, "c":3}'::jsonb - 'c'::text;
select '{"a":1 , "b":2, "c":3}'::jsonb - 'd'::text;
select pg_column_size('{"a":1 , "b":2, "c":3}'::jsonb - 'b'::text) = pg_column_size('{"a":1, "b":2}'::jsonb);

select '["a","b","c"]'::jsonb - 3;
select '["a","b","c"]'::jsonb - 2;
select '["a","b","c"]'::jsonb - 1;
select '["a","b","c"]'::jsonb - 0;
select '["a","b","c"]'::jsonb - -1;
select '["a","b","c"]'::jsonb - -2;
select '["a","b","c"]'::jsonb - -3;
select '["a","b","c"]'::jsonb - -4;

select '{"a":1, "b":2, "c":3}'::jsonb - 3;
select '{"a":1, "b":2, "c":3}'::jsonb - 2;
select '{"a":1, "b":2, "c":3}'::jsonb - 1;
select '{"a":1, "b":2, "c":3}'::jsonb - 0;
select '{"a":1, "b":2, "c":3}'::jsonb - -1;
select '{"a":1, "b":2, "c":3}'::jsonb - -2;
select '{"a":1, "b":2, "c":3}'::jsonb - -3;
select '{"a":1, "b":2, "c":3}'::jsonb - -4;

select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{n}', '[1,2,3]');
select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '[1,2,3]');
select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,1,0}', '[1,2,3]');
select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,NULL,0}', '[1,2,3]');

select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{n}', '{"1": 2}');
select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '{"1": 2}');
select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,1,0}', '{"1": 2}');
select jsonb_replace('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,NULL,0}', '{"1": 2}');
