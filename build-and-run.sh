#!/bin/bash 

if [ "$1" = "web" ]; then
    BUILD=build-web
    emcmake cmake -DPLATFORM=Web -G Ninja -S . -B build-web
else 
    BUILD=build
    cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build
fi



while true; do
    if cmake --build $BUILD; then
        if [ "$BUILD" = "build-web" ]; then 
            (cd build-web/raylib-game-template; python3 -m http.server 9000) || exit
        else 
            ./build/raylib-game-template/raylib-game-template || exit
        fi
    else
        inotifywait -e modify -r src/
    fi
done
