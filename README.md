Welches Projekt was macht und sie zusammenh\ngen

Overview
========

This project contains the filesystem Paffs and its drivers for interfacing simulated or real hardware.

- ./config
 -- Example configuration files
- ./doc
 -- Documentation of filesystem structure (`make doc`)
- ./ext
 -- Snapshot of spacewire, amap and nand drivers. Will be deprecated with newer outpost-platform-leon
- ./it
 -- Integration tests (can be used as examples for using filesystem)
- ./src
 -- Source files
- ./test
 -- Unit test


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

Note: The office_model2 integration test is designed for a combination of the OM1-IFF connected to SPW1 (the lower connector) of the OM2-Leon.

Expected structure
------
- paffs/
- scons-build-tools/
- outpost-core/

Optional, for integration tests:
- satfon-simulation/    (Also contains flash and mram viewer which autoconnect to simulated memory) 

If wanted, paffs can be compared and tested in different scenarios such as wear levelling and Journalling by checking out 'satfon' project, which would be located beside other projects.