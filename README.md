How to use
==========

This Filesystem expects an outpost-core and a scons-build-tools checkout in the parent directory.
For Integration Tests, it needs the satfon-simulation repo in the parent directory.

To build with a special driver, modify the SConstruct file.

To build, run make.
For Unittests, run make unit-test.
For all Tests, run make test. (requires satfon-simulation)


/
-paffs/
-scons-build-tools/
-outpost-core/
-satfon-simulation/
