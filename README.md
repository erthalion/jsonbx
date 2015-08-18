[![Build Status](https://travis-ci.org/erthalion/jsonbx.svg?branch=master)](https://travis-ci.org/erthalion/jsonbx)

jsonbx
======

As you know, PostgreSQL introduced Jsonb [support](http://obartunov.livejournal.com/177247.html) at the 9.4 version, and hstore v2.0 saved in separate [repository](http://www.sigaev.ru/git/gitweb.cgi?p=hstore.git;a=summary). But although PostgreSQL has this support at the core level, there are many useful functions, which wasn't implemented for Jsonb, particularly there are not so many functions and operators for manipulation with jsonb. This repo will accumulate the implementation such kind of functions. Part of this functionality was already incorporated into PostgreSQL 9.5 (see this [commit](http://git.postgresql.org/gitweb/?p=postgresql.git;a=commit;h=c6947010ceb42143d9f047c65c1eac2b38928ab7)).


List of implemented functions
---------------------------------

* jsonb_pretty (in 9.5)
* jsonb_concat (in 9.5)
* jsonb_delete(jsonb, text) (in 9.5)
* jsonb_delete_idx(jsonb, int) (in 9.5)
* jsonb_delete_path(jsonb, text[]) (in 9.5)
* jsonb_set(jsonb, text[], jsonb) (in 9.5)

List of implemented operators
---------------------------------

* concatenation operator (||) (in 9.5)
* delete key operator (jsonb - text) (in 9.5)
* delete key by index operator (jsonb - int) (in 9.5)
* delete key by path operator (jsonb - text[]) (in 9.5)

License
-------

jsonbx is licensed under [the same license as PostgreSQL itself](http://www.postgresql.org/about/licence/)

Contributors
------------

jsonbx was created by Dmitry Dolgov
portions written by Andrew Dunstan
