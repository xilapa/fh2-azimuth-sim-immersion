#!/usr/bin/env bash
# Host-side unit tests (also run by build.sh and .github/workflows/test.yml).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
g++ -std=c++17 -O1 -Wall tests/test_pose.cpp -o build/test_pose && ./build/test_pose
g++ -std=c++17 -O1 -Wall tests/test_ffb.cpp src/dll/wheel_ffb.cpp -o build/test_ffb && ./build/test_ffb
g++ -std=c++17 -O1 -Wall tests/test_ini.cpp -o build/test_ini && ./build/test_ini
g++ -std=c++17 -O1 -Wall tests/test_lock.cpp src/dll/wheel_ffb.cpp -o build/test_lock && ./build/test_lock
g++ -std=c++17 -O1 -Wall tests/test_cars.cpp -o build/test_cars && ./build/test_cars
