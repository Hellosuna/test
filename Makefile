# contrib/wamr/Makefile

MODULE_big = wamr
OBJS= wamr.o

EXTENSION = wamr
DATA = wamr--1.0.sql

SHLIB_LINK_INTERNAL = $(libpq)
SHLIB_LINK += -liwasm -lvmlib 

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/wamr
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
override CPPFLAGS := $(filter-out -fPIE, $(CPPFLAGS)) -fPIC
endif
