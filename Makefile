
ifneq ($(JOBS),)
  MAKEJOBS=-j$(JOBS)
else
  # Default is 3 parallel jobs. Use `JOBS=` to disable parallel build.
  NPROCS:=3
  OS:=$(shell uname -s)
  ifeq ($(OS),Linux)
    NPROCS:=$(shell nproc)
  endif
  MAKEJOBS=-j$(NPROCS)
endif

TESTDIR=./test/
OM1INTEGRATIONDIR=./it/office_model
OM2INTEGRATIONDIR=./it/office_model2
INTEGRATIONDIR=./it/logic
MISCDIR=./it/misc

all: build-integration-debug build-misc

build-embedded-om1:
	@scons $(MAKEJOBS) -C $(OM1INTEGRATIONDIR)
build-embedded-om2:
	@scons $(MAKEJOBS) -C $(OM2INTEGRATIONDIR)

build-integration:
	@scons $(MAKEJOBS) -C $(INTEGRATIONDIR) --release-build

build-integration-debug:
	@scons $(MAKEJOBS) -C $(INTEGRATIONDIR)

build-integration-bigflash:
	@scons $(MAKEJOBS) -C $(INTEGRATIONDIR) --release-build --bigflash

build-integration-bigflash-debug:
	@scons $(MAKEJOBS) -C $(INTEGRATIONDIR) --bigflash

build-unittest:
	@scons $(MAKEJOBS) -C $(TESTDIR) --release-build

build-unittest-debug:
	@scons $(MAKEJOBS) -C $(TESTDIR)

build-misc:
	@scons $(MAKEJOBS) -C $(MISCDIR)

test: test-unit test-integration

test-unit: build-unittest
	./build/release/unit/unittest --gtest_output=xml:./build/release/test/unit_coverage.xml

test-integration: build-integration
	./build/release/it/integrationtest --gtest_output=xml:./build/release/test/integration_coverage.xml

test-integration-bigflash: build-integration-bigflash
	./build/release/it/bigflash/integrationtest --gtest_output=xml:./build/release/test/integration_coverage.xml

test-embedded-om1: build-embedded-om1
	/opt/grmon-eval-2.0.83/linux64/bin/grmon -uart /dev/cobc_dsu_3 -stack 0x40fffff0 -baud 460800 -gdb

test-embedded-om2: build-embedded-om2
	/opt/grmon-eval-2.0.83/linux64/bin/grmon -uart /dev/tty_rcn0001_dsu -stack 0x40fffff0 -baud 460800 -gdb

test-misc: build-misc
	./build/debug/misc/misctest

get-dep:
	if [ ! -d "../outpost-core" ]; then git clone ssh://git@github.com:DLR-RY/outpost-core.git ../outpost-core; fi
	if [ ! -d "../satfon-simulation" ]; then git clone ssh://git@gitlab.informatik.uni-bremen.de:ppieper/satfon-simulation.git ../satfon-simulation; fi
	if [ ! -d "../scons-build-tools" ]; then git clone ssh://git@github.com:DLR-RY/scons-build-tools.git ../scons-build-tools; fi
	if [ ! -d "../xetex-document-base" ]; then ssh://git@hbryavsci1l.hb.dlr.de:10022/avionics-software/xetex-document-base.git ../xetex-document-base; fi
doc:
	@$(MAKE) -C doc/PAFFS_DOCUMENTATION

.PHONY: doc

clean:
	@scons -C $(TESTDIR) -c
	@scons -C $(INTEGRATIONDIR) -c
	@scons -C $(OM1INTEGRATIONDIR) -c
	@scons -C $(OM2INTEGRATIONDIR) -c
	rm -rf build/
	@$(MAKE) -C doc/PAFFS_DOCUMENTATION clean
