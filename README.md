How to use
==========

This Filesystem expects an outpost-core and a scons-build-tools checkout in the parent directory.
For Integration Tests, it needs the satfon-simulation repo in the parent directory.

To build with a special driver, modify the SConstruct file.

- For cloning all necessary repositories in parent dir, run make get-dep
- For Unittests, run make test.
- For Integration Tests, run make test-it.

Expected structure
------
- paffs/
- scons-build-tools/
- outpost-core/
- satfon-simulation/
