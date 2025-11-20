#!/bin/bash
# WebAssembly Build Script for Fourier Epicycles Project
# Compiles C source files to WebAssembly using Emscripten

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if Emscripten is available
if ! command -v emcc &> /dev/null; then
    echo -e "${RED}Error: emcc not found${NC}"
    echo "Please use one of these options:"
    echo ""
    echo "Option 1: Use Docker (recommended for Alpine/musl systems):"
    echo "  ./scripts/build_web_docker.sh"
    echo ""
    echo "Option 2: Install Emscripten SDK manually (glibc systems only):"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

echo -e "${GREEN}=== Building Fourier Epicycles for WebAssembly ===${NC}"

# Configuration
BUILD_DIR="web-build"
RAYLIB_SRC="$HOME/.cache/zig/raylib"  # Adjust if needed
COMMON_FLAGS="-Os -Wall -std=c99"
COMMON_DEFINES="-DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2"
EMCC_FLAGS="-s USE_GLFW=3 -s ASYNCIFY -s TOTAL_MEMORY=134217728 -s FORCE_FILESYSTEM=1 -s ASSERTIONS=1"
EMCC_LIBS="-s EXPORTED_RUNTIME_METHODS=ccall"

# Create build directory
mkdir -p "$BUILD_DIR"

echo -e "${YELLOW}Building math library...${NC}"

# Compile math library object files
emcc $COMMON_FLAGS $COMMON_DEFINES \
    -I src \
    -c src/fft.c -o "$BUILD_DIR/fft.o"

emcc $COMMON_FLAGS $COMMON_DEFINES \
    -I src \
    -c src/fourier.c -o "$BUILD_DIR/fourier.o"

emcc $COMMON_FLAGS $COMMON_DEFINES \
    -I src \
    -c src/path_loader.c -o "$BUILD_DIR/path_loader.o"

# Check if we need to build Raylib
if [ ! -f "$BUILD_DIR/libraylib.web.a" ]; then
    echo -e "${YELLOW}Building Raylib for WebAssembly...${NC}"
    echo "This will take a few minutes on first build..."

    # Clone or update Raylib if not present
    if [ ! -d "$BUILD_DIR/raylib" ]; then
        git clone --depth 1 https://github.com/raysan5/raylib.git "$BUILD_DIR/raylib"
    fi

    cd "$BUILD_DIR/raylib/src"

    # Build Raylib with Emscripten
    make clean
    make PLATFORM=PLATFORM_WEB RAYLIB_LIBTYPE=STATIC -j$(nproc)

    # Copy library (Raylib creates libraylib.web.a for PLATFORM_WEB)
    cp libraylib.web.a ../../
    cd ../../..

    echo -e "${GREEN}Raylib built successfully${NC}"
else
    echo -e "${GREEN}Using cached Raylib library${NC}"
fi

# Function to build an example
build_example() {
    local NAME=$1
    local SOURCE=$2
    local SHELL_FILE=$3

    echo -e "${YELLOW}Building $NAME...${NC}"

    emcc $COMMON_FLAGS $COMMON_DEFINES \
        -I src \
        -I "$BUILD_DIR/raylib/src" \
        $SOURCE \
        "$BUILD_DIR/fft.o" \
        "$BUILD_DIR/fourier.o" \
        "$BUILD_DIR/path_loader.o" \
        "$BUILD_DIR/libraylib.web.a" \
        $EMCC_FLAGS \
        $EMCC_LIBS \
        --preload-file examples/assets \
        --shell-file "$SHELL_FILE" \
        -o "$BUILD_DIR/${NAME}.html"

    echo -e "${GREEN}✓ $NAME built successfully${NC}"
    echo "   Output: $BUILD_DIR/${NAME}.html"
}

# Build examples
build_example "simple_example" "examples/simple_example/main.c" "examples/simple_example/shell.html"
build_example "museum" "examples/museum/main.c" "examples/museum/shell.html"

echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""
echo "To test locally, run:"
echo "  cd $BUILD_DIR"
echo "  python3 -m http.server 8080"
echo ""
echo "Then open in browser:"
echo "  http://localhost:8080/simple_example.html"
echo "  http://localhost:8080/museum.html"
