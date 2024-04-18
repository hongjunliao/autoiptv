autoiptv - auto update .m3u file
=============

# compile and run

* configure and build

```code
# make build dir: ${autoiptv}/build
$ mkdr build && cd build;
# configure on Linux, build debug version, set CMAKE_BUILD_TYPE=Release to a release verison
$ cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && make -j 2
```

* run it!

```code
    ${autoiptv}/build/autoiptv --m3u=cctv.m3u --url=url.txt
```

# 实现简要说明

  * 根据cctv.m3u频道关键字去url.txt中的URL指定的.m3u文件中查找频道URL，并更新到cctv.m3u,更新完成后，使用mpv cctv.m3u即可播放

# 依赖库

* https://gitee.com/jun/libhp.git
