CREATE EXTENSION jsonbx;

select jsonb_print('{"a": "test", "b": [1, 2, 3], "c": "test3", "d":{"dd": "test4", "dd2":{"ddd": "test5"}}}'::jsonb, true);
select jsonb_concat('{"d": "test", "a": [1, 2]}'::jsonb, '{"g": "test2", "c": {"c1":1, "c2":2}}'::jsonb);
