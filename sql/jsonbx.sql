CREATE EXTENSION jsonbx;

select jsonb_pretty('{"a": "test", "b": [1, 2, 3], "c": "test3", "d":{"dd": "test4", "dd2":{"ddd": "test5"}}}'::jsonb);

select jsonb_concat('{"d": "test", "a": [1, 2]}'::jsonb, '{"g": "test2", "c": {"c1":1, "c2":2}}'::jsonb);

select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"cq":"l", "b":"g", "fg":false}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"aq":"l"}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"aa":"l"}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{}';

select '["a", "b"]'::jsonb || '["c"]';
select '["a", "b"]'::jsonb || '["c", "d"]';
select '["c"]' || '["a", "b"]'::jsonb;

select '["a", "b"]'::jsonb || '"c"';
select '"c"' || '["a", "b"]'::jsonb;

select '"a"'::jsonb || '{"a":1}';
select '{"a":1}' || '"a"'::jsonb;

select '["a", "b"]'::jsonb || '{"c":1}';
select '{"c": 1}'::jsonb || '["a", "b"]';

select '{}'::jsonb || '{"cq":"l", "b":"g", "fg":false}';

select pg_column_size('{}'::jsonb || '{}'::jsonb) = pg_column_size('{}'::jsonb);
select pg_column_size('{"aa":1}'::jsonb || '{"b":2}'::jsonb) = pg_column_size('{"aa":1, "b":2}'::jsonb);
select pg_column_size('{"aa":1, "b":2}'::jsonb || '{}'::jsonb) = pg_column_size('{"aa":1, "b":2}'::jsonb);
select pg_column_size('{}'::jsonb || '{"aa":1, "b":2}'::jsonb) = pg_column_size('{"aa":1, "b":2}'::jsonb);

select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'a');
select jsonb_delete('{"a":1}'::jsonb, 'a');
select jsonb_delete('"a"'::jsonb, 'a');
select jsonb_delete('{"a":null , "b":2, "c":3}'::jsonb, 'a');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'b');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'c');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'd');
select '{"a":1}'::jsonb - 'a'::text;
select '"a"'::jsonb - 'a'::text;
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

select jsonb_delete('{"a":1, "b":2, "c":3}'::jsonb, '{d, e}'::text[]);
select jsonb_delete('{"a":1, "b":2, "c":3}'::jsonb, '{b}'::text[]);
select jsonb_delete('{"a":{"c":1, "d": 2}, "b":3}'::jsonb, '{a, c}'::text[]);
select jsonb_delete('{"a":{"c":1, "d":{"f": 3, "g": 4}}, "b":5}'::jsonb, '{a, d, g}'::text[]);
select jsonb_delete('{"a":1, "b":2, "c":3}'::jsonb, '{}'::text[]);
select '{"a":1, "b":2, "c":3}'::jsonb - '{d, e}'::text[];
select '{"a":1, "b":2, "c":3}'::jsonb - '{b}'::text[];
select '{"a":{"c":1, "d":2}, "b":3}'::jsonb - '{a, c}'::text[];
select '{"a":{"c":1, "d":{"f": 3, "g": 4}}, "b":5}'::jsonb - '{a, d, g}'::text[];
select '{"a":1, "b":2, "c":3}'::jsonb - '{}'::text[];
select pg_column_size('{"a":1, "b":2, "c":3}'::jsonb - '{a}'::text[])
         = pg_column_size('{"b":2, "c":3}'::jsonb);
select pg_column_size('{"a":1, "b":2, "c":3}'::jsonb - '{}'::text[])
         = pg_column_size('{"a":1, "b":2, "c":3}'::jsonb);

select jsonb_set('"a"'::jsonb, '{a}', '[1,2,3]');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{n}', '[1,2,3]');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '[1,2,3]');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,1,0}', '[1,2,3]');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,NULL,0}', '[1,2,3]');

select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{n}', '{"1": 2}');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '{"1": 2}');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,1,0}', '{"1": 2}');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,NULL,0}', '{"1": 2}');

select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '"test"');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '{"f": "test"}');

-- empty structure and error conditions for delete and replace

select '"a"'::jsonb - 'a'; -- error
select '{}'::jsonb - 'a';
select '[]'::jsonb - 'a';
select '"a"'::jsonb - 1; -- error
select '{}'::jsonb -  1 ;
select '[]'::jsonb - 1;
select '"a"'::jsonb - '{a}'::text[]; -- error
select '{}'::jsonb - '{a}'::text[];
select '[]'::jsonb - '{a}'::text[];
select jsonb_set('"a"','{a}','"b"'); --error
select jsonb_set('{}','{a}','"b"', false);
select jsonb_set('[]','{1}','"b"', false);

-- jsonb_set adding instead of replacing

-- prepend to array
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{b,-33}','{"foo":123}');
-- append to array
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{b,33}','{"foo":123}');
-- check nesting levels addition
select jsonb_set('{"a":1,"b":[4,5,[0,1,2],6,7],"c":{"d":4}}','{b,2,33}','{"foo":123}');
-- add new key
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{c,e}','{"foo":123}');
-- adding doesn't do anything if elements before last aren't present
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{x,-33}','{"foo":123}');
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{x,y}','{"foo":123}');
-- add to empty object
select jsonb_set('{}','{x}','{"foo":123}');
--add to empty array
select jsonb_set('[]','{0}','{"foo":123}');
select jsonb_set('[]','{99}','{"foo":123}');
select jsonb_set('[]','{-99}','{"foo":123}');

-- verify path correctness
select jsonb_set('{"a": [1, 2, 3]}', '{a, non_integer}', '"new_value"');
select jsonb_set('{"a": {"b": [1, 2, 3]}}', '{a, b, non_integer}', '"new_value"');
select jsonb_set('{"a": {"b": [1, 2, 3]}}', '{a, b, NULL}', '"new_value"');
