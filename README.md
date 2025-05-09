# vmax2bella

Command line convertor from [VoxelMax](https://voxelmax.com) .vmax to DiffuseLogic's [Bella](https://bellarender.com) .bsz

![example](resources/example.jpg)


# Precompiled binaries ( Alpha release )
[MacOS](https://a4g4.c14.e2-1.dev/vmax2bella/vmax2bella_macuniversal_0.1.zip)

[Windows](https://a4g4.c14.e2-1.dev/vmax2bella/vmax2bella_win_alpha0.1.zip)


# Usage

```
./vmax2bella -h  // show help
./vmax2bella -i:bear.vmax // convert bear.vmax to bear.bsz using cubes
./vmax2bella -i:bear.vmax --mode:mesh // convert to bear.bsz using mesh
./vmax2bella -i:bear.vmax --mode:mesh --bevel // convert to bear.bsz using mesh and bevel shader
```

VoxelMax features supported
- metallness converted to Bella metal quickmaterial (not PBR), roughness supported
- Material 7 is Glass
- Material 8 is Liquid and is always a mesh
- 0 roughness is converted to Oren-Nayar diffuse
- opacity < 255 turns into Glass
- roughness > 0 converted to Bella Plastic
- emitter supported

Load **bear.bsz** into [bella_gui](https://bellarender.com/builds) for rendering

- [TODO] convert scene.json camera
- [TODO] output one .bsz file per chunk timeline for anim
- [TODO] convert chunk camera for anim





# Build

Download SDK for your OS and move **bella_scene_sdk** into your **workdir**. On Windows rename unzipped folder by removing version ie bella_engine_sdk-24.6.0 -> bella_scene_sdk
https://bellarender.com/builds/


```
workdir/
├── bella_scene_sdk/
├── libplist/
├── lzfse/
├── opengametools/
└── vmax2bella/
```

# MacOS

```
mkdir workdir
git clone https://github.com/lzfse/lzfse
mkdir -p lzfse/build
cd lzfse/build
/Applications/CMake.app/Contents/bin/cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
make -j4
cd ../..
mkdir homebrew
curl -L https://github.com/Homebrew/brew/tarball/master | tar xz --strip-components 1 -C homebrew
eval "$(homebrew/bin/brew shellenv)"
brew update --force --quiet
brew install libtool autoconf automake
git clone https://github.com/libimobiledevice/libplist.git
cd libplist
export CFLAGS="-arch arm64 -arch x86_64"                                        
export CXXFLAGS="-arch arm64 -arch x86_64"
export LDFLAGS="-arch arm64 -arch x86_64"
./autogen.sh --prefix=$PWD/install --without-cython
make -j4
install_name_tool -id @rpath/libplist-2.0.4.dylib src/.libs/libplist-2.0.4.dylib
cd ..
git clone https://github.com/jpaver/opengametools.git
git clone https://github.com/oomer/oom.git
git clone https://github.com/oomer/vmax2bella.git
cd vmax2bella
make all -j4
install_name_tool -change ../lzfse/build/liblzfse.dylib @rpath/liblzfse.dylib bin/Darwin/release/vmax2bella
install_name_tool -change /usr/local/lib/libplist-2.0.4.dylib @rpath/libplist-2.0.4.dylib bin/Darwin/release/vmax2bella

```

# Linux [NOT READY]

```
mkdir workdir
git clone https://github.com/lzfse/lzfse
mkdir lzfse/build
cd lzfse/build
cmake ..
make -j4
cd ../..
git clone https://github.com/libimobiledevice/libplist.git
cd libplist
./autogen.sh --prefix=$PWD/install --without-cython
make -j4
cd ..
git clone https://github.com/jpaver/opengametools.git
git clone https://github.com/oomer/oom.git
git clone https://github.com/oomer/vmax2bella.git
cd vmax2bella
make
```

# Windows 
- Install Visual Studio Community 2022
- Add Desktop development with C++ workload
- Launch x64 Native tools Command Prompt for VS2022
```
mkdir workdir
git clone https://github.com/lzfse/lzfse
mkdir -p lzfse/build
cd lzfse/build
cmake ..
msbuild lzfse.vcxproj /p:Configuration=release /p:Platform=x64 /p:PlatformToolset=v143
cd ../..
git clone https://github.com/oomer/libplist
git clone https://github.com/jpaver/opengametools.git
git clone https://github.com/oomer/oom.git
git clone https://github.com/oomer/vmax2bella.git
cd libplist
msbuild libplist.vcxproj /p:Configuration=release /p:Platform=x64 /p:PlatformToolset=v143
cd ..
cd vmax2bella
msbuild vmax2bella.vcxproj /p:Configuration=release /p:Platform=x64 /p:PlatformToolset=v143
```
