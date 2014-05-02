MODULES = jsonbx

DATA = jsonbx--1.0.sql
EXTENSION = jsonbx

REGRESS = jsonbx

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
