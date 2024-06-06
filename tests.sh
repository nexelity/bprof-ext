#!/bin/bash -ex

# Define the Dockerfile and image names
declare -a dockerfiles=(
    "tests/Dockerfile-8.0-alpine"
    "tests/Dockerfile-8.0-buster"
    "tests/Dockerfile-8.0-bullseye"
    "tests/Dockerfile-8.1-alpine"
    "tests/Dockerfile-8.1-buster"
    "tests/Dockerfile-8.1-bullseye"
    "tests/Dockerfile-8.1-bookworm"
    "tests/Dockerfile-8.2-alpine"
    "tests/Dockerfile-8.2-buster"
    "tests/Dockerfile-8.2-bullseye"
    "tests/Dockerfile-8.2-bookworm"
    "tests/Dockerfile-8.3-alpine"
    "tests/Dockerfile-8.3-bullseye"
    "tests/Dockerfile-8.3-bookworm"
)

# Cleanup any spillover
docker rm -f dummy || true
rm -rf outputs/
mkdir -p outputs/

# Define the corresponding image names
declare -a images=(
    "alpine/8.0"
    "buster/8.0"
    "bullseye/8.0"
    "alpine/8.1"
    "buster/8.1"
    "bullseye/8.1"
    "bookworm/8.1"
    "alpine/8.2"
    "buster/8.2"
    "bullseye/8.2"
    "bookworm/8.2"
    "alpine/8.3"
    "bullseye/8.3"
    "bookworm/8.3"
)

# Define the architectures to test
declare -a archs=(
    "linux/arm64"
    "linux/amd64"
    "linux/arm64/v8"
    "linux/arm/v7"
    "linux/386"
    "linux/mips64le"
    "linux/ppc64le"
    "linux/s390x"
)

# Loop over the architectures
for arch in "${archs[@]}"; do
    # Loop over the Dockerfiles and images to build and run the tests
    for i in "${!dockerfiles[@]}"; do
        echo "Testing ${images[$i]} on architecture: $arch"
        # Build the Docker image for the specific architecture
        docker buildx build -q --platform $arch -f "${dockerfiles[$i]}" . -t "${images[$i]}-${arch##*/}" --load

        # Create a temporary file to hold the output
        output=$(mktemp)

        # Run the Docker container and capture the output
        if ! docker run -v "$(pwd)/tests:/tests" -it --rm "${images[$i]}-${arch##*/}" php /tests/TestSuite.php > "$output" 2>&1; then
            echo "Test failed for image: ${images[$i]} on architecture: $arch"
            cat "$output"
            exit 1
        fi

        # Remove the temporary file
        rm "$output"

        # Grab the build
        mkdir -p "outputs/${arch}/${images[$i]}"
        ext_path=$(docker run -v "$(pwd):/build" --name=dummy -it "${images[$i]}-${arch##*/}" php-config --extension-dir | tr -d '\r')
        docker cp "dummy:${ext_path}/bprof.so" "outputs/${arch}/${images[$i]}/bprof.so"
        docker rm -f dummy || true
    done
done

docker system prune -f