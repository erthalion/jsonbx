MODULE_big = jsonbx
OBJS = jsonbx.o jsonbx_utils.o

DATA = jsonbx--1.0.sql
EXTENSION = jsonbx

REGRESS = jsonbx

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
