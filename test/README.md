# DBMail unit tests

To reduce breaking changes, these checks should be run before committing any
changes to the git repository, ideally as part of an automated build script.

As a minimum, these checks should be run before distro maintainers release a
new package.

Package maintainers should consider using the check.sh script in test-scripts.

DBMail is a collection of services with many configuration options. These
checks only test functional units.
