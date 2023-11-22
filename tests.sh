#!/bin/bash -xe

docker build -f tests/Dockerfile-test-8.0 . -t bprof80
docker run -v ./tests:/tests --rm -it bprof80 php /tests/TestSuite.php

docker build -f tests/Dockerfile-test-8.1 . -t bprof81
docker run -v ./tests:/tests --rm -it bprof81 php /tests/TestSuite.php

docker build -f tests/Dockerfile-test-8.2 . -t bprof82
docker run -v ./tests:/tests --rm -it bprof82 php /tests/TestSuite.php
