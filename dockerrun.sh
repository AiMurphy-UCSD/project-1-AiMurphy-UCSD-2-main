#!/bin/bash

# Rebuild docker image if necessary (quietly)
docker build -q -t project-1a . 2>/dev/null >/dev/null

# Run any command inside the Docker container with the project mounted.
# Examples:
#   ./run.sh make
#   ./run.sh ./tritontalk test_suite/basic.cfg
#   ./run.sh ./tester.sh
docker run --rm -v "$(pwd)":/project -w /project project-1a "$@"
