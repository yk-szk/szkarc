mkdir build
cd build
mkdir bin
cmake .. -DEXECUTABLE_OUTPUT_PATH=bin
CMAKE_RUNTIME_OUTPUT_DIRECTORY=bin cmake --build . --config Release --parallel
