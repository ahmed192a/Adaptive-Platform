# Adaptive 

![example workflow](https://github.com/ahmed192a/Adaptive/actions/workflows/master.yml/badge.svg)

_Adaptive Autosar Platform_ enviroment over Linux defined by [AUTOSAR](https://www.autosar.org/standards/adaptive-platform/).<br>
* For more information about the project refer to [wiki](https://github.com/ahmed192a/Adaptive/wiki)
* For Project Documentation refer to [Github pages](https://ahmed192a.github.io/Adaptive)

-----
## Build (CMake presets)

```bash
cmake --preset debug && cmake --build --preset debug -j && ctest --preset debug
# other presets: release | asan (ASan+UBSan) | coverage | ci (-Werror on new clusters)
```

Key options (see `cmake/AdaptiveOptions.cmake`): `AP_BUILD_TESTS`, `AP_BUILD_APPS`, `AP_BUILD_TOOLS`,
`AP_BUILD_EXAMPLES`, `AP_ENABLE_SANITIZERS`, `AP_ENABLE_COVERAGE`, `AP_USE_SYSTEM_DEPS`, `AP_INSTALL`.
Installed packages are consumed with `find_package(AdaptivePlatform)` and
`target_link_libraries(app PRIVATE AdaptivePlatform::core AdaptivePlatform::phm ...)`.
Add a cluster with `ap_add_cluster(NAME x DEPENDS ...)` in `src/ara/x/CMakeLists.txt`.

Cluster status and the AUTOSAR gap analysis live in [doc/ROADMAP.md](doc/ROADMAP.md).

-----
## General steps

- to use the ".sh" files you need first to run this command in terminal 
```bash
chmod +x *.sh
```
- you have to cinfigure the project before any thing
- build and run the project

-------------

### First of all

- open terminal in the project dir
- change the dir to quick_test 
```bash
cd quick_test
```

### steps for configuration

- run this command 
```bash
./configure.sh
```

### steps for building

- run this command 
```bash
./build.sh
```

### steps for running

- run this command 
```bash
./run.sh
```

### steps for testing

- run this command 
```bash
./test.sh
```

--------
## steps for SD test

- Run this command just one time
```bash
./configure.sh
```
```bash
./build.sh
./sd_test.sh
```
