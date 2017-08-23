
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
INTEGRATIONDIR=./it/office_model

all: get-dep test test-it doc

build-integration:
	@scons $(MAKEJOBS) -C $(INTEGRATIONDIR)

build-test:
	@scons $(MAKEJOBS) -C $(TESTDIR) --release-build
	
build-debugtest:
	@scons $(MAKEJOBS) -C $(TESTDIR)

test: build-test
	./build/release/unittest --gtest_output=xml:./build/release/test/coverage.xml

test-it: build-integration
	#To be added
	#/opt/grmon-eval-2.0.83/linux64/bin/grmon -uart /dev/cobc_dsu_2 -stack 0x40fffff0 -baud 460800 -gdb

it-deploy: build-integration
	/opt/grmon-eval-2.0.83/linux64/bin/grmon -uart /dev/cobc_dsu_2 -stack 0x40fffff0 -baud 460800 -gdb

get-dep:
	if [ ! -d "../outpost-core" ]; then git clone ssh://git@hbryavsci1l.hb.dlr.de:10022/avionics-software-open/outpost-core.git ../outpost-core; fi
	if [ ! -d "../satfon-simulation" ]; then git clone ssh://git@hbryavsci1l.hb.dlr.de:10022/avionics-software-open/satfon-simulation.git ../satfon-simulation; fi
	if [ ! -d "../scons-build-tools" ]; then git clone ssh://git@hbryavsci1l.hb.dlr.de:10022/avionics-software-open/scons-build-tools.git ../scons-build-tools; fi

doc:
	@$(MAKE) -C doc/PAFFS_DOCUMENTATION

clean:
	@scons -C $(TESTDIR) -c
	@scons -C $(INTEGRATIONDIR) -c
	@$(MAKE) -C doc/PAFFS_DOCUMENTATION clean
