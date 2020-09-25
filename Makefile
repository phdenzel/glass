
ROOT_DIR := $(PWD)
GLPK_DIST ?= glpk-4.47.tar.gz  
PYTHON_GLPK_DIST ?= python-glpk-0.4.43.tar.gz

GLPK_SRC_DIR := glpk-4.47
PYTHON_GLPK_SRC_DIR := python-glpk-0.4.43
PYTHON_INC=$(shell python -c "from distutils.sysconfig import get_python_inc; print get_python_inc(plat_specific=0)")
PYTHON_LIB=$(shell python -c "from distutils.sysconfig import get_python_lib; print get_python_lib(plat_specific=0)")
CPATH := "$(PYTHON_INC):$(PYTHON_LIB)/../config"
UNAME ?= $(shell uname)
LIBRARY_PATH := $(ROOT_DIR)/build/glpk_build/lib:$(ROOT_DIR)/build/glpk_build/lib64

ifeq ($(UNAME),Darwin)
  GLPK_FLAGS="CFLAGS=-arch x86_64"
	#GLPK_FLAGS="CFLAGS=-arch i386"
	#GLPK_FLAGS="CFLAGS=-arch i386 -arch x86_64"
	CHANGE_PGLPK_MAKEFILE=cp $(ROOT_DIR)/lib/python-glpk-swig-Makefile swig/Makefile
	CHANGE_PGLPK_SETUP="true"
else
	GLPK_FLAGS=
#  CHANGE_PGLPK_MAKEFILE="true"
  CHANGE_PGLPK_MAKEFILE=cp $(ROOT_DIR)/lib/python-glpk-swig-linux-Makefile swig/Makefile
	CHANGE_PGLPK_SETUP=cp $(ROOT_DIR)/lib/python-glpk-setup.py setup.py
endif

all: glpk python-glpk glass

glpk:
	(cd lib \
	&& tar xvzf $(GLPK_DIST) \
	&& cd $(GLPK_SRC_DIR) \
	&& ./configure --prefix=$(ROOT_DIR)/build/glpk_build $(GLPK_FLAGS) --disable-dependency-tracking \
	&& make \
	&& make install)

python-glpk:
	(cd lib \
	&& tar xvzf $(PYTHON_GLPK_DIST) \
	&& cd $(PYTHON_GLPK_SRC_DIR)/src \
	&& export CPATH=$(CPATH) \
	&& export LIBRARY_PATH=$(LIBRARY_PATH) \
  && export GLPK_LIB_DIR=$(LIBRARY_PATH) \
  && export GLPK_INC_DIR=$(LIBRARY_PATH)/../include \
	&& $(CHANGE_PGLPK_MAKEFILE) \
	&& $(CHANGE_PGLPK_SETUP) \
	&& make -C swig all \
	&& cp swig/glpkpi.py python/glpkpi.py \
	&& python setup.py build --build-base=$(ROOT_DIR)/build/python-glpk)

glass:
	python setup.py build 

.PHONY: glass

clean-build:
	rm -rf build/

clean-lib:
	rm -rf lib/$(GLPK_SRC_DIR) lib/$(PYTHON_GLPK_SRC_DIR)

clean: clean-build

clean-all: clean-build clean-lib
