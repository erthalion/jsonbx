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
