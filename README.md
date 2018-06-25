Welches Projekt was macht und sie zusammenh\ngen

Overview
========

This project contains the filesystem Paffs and its drivers for interfacing simulated or real hardware.

./config
- 


How to use
==========

This Filesystem expects an outpost-core and a scons-build-tools checkout in the parent directory.
For Integration Tests, it needs the satfon-simulation repo in the parent directory.
To run generic filesystem-exhausting tests on paffs, refer to "satfon" project.

To build with a special driver, modify the SConstruct file of your project. For examples see test/SConstruct or it/*/SConstruct, all of which use different drivers.

- For cloning all necessary repositories in parent dir, run `make get-dep`
- For Unittests, run `make test`
- For Integration Tests, run `make test-it`
- For building the nexys3-based integration test, run `make test-embedded-om1`
- For building the artix7-based integration test, run `make test-embedded-om2`

Expected structure
------
- paffs/
- scons-build-tools/
- outpost-core/

Optional for integration tests:
- satfon-simulation/        (simulation)
