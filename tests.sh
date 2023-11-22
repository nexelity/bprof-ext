#!/bin/bash
docker build -f Dockerfile-test-8.0 . --rm
docker build -f Dockerfile-test-8.1 . --rm
docker build -f Dockerfile-test-8.2 . --rm