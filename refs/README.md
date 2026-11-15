# `refs/` —— 外部参考工程（只读，不进版本控制）

这里放的是**别人的代码**，本项目的 `cli/` 只链接/参考其中一部分。
每个子目录都是独立的上游仓库（自带 `.git`），`.gitignore` 里整体排除，
所以 clone 本仓库不会带上它们——需要时按下面重新拉。

```
refs/
├── 0x808/          averagenative/0x808        ImGui 音序器壳（MIT）
├── libvgm/         valleybell/libvgm          VGM 解析 + 芯片仿真（★ 构建依赖）
├── furnace/        tildearrow/furnace         .fui 格式来源（GPL-2.0+，只复制 4 个文件）
├── FamiStudio/     BleuBleu/FamiStudio        参考用，无 YM2151
├── Gridline/       lolitemaultes/Gridline     不量化的时间模型参考
├── grauw/          grauw/*                    48 个小仓库（vgmtransmute 等）
├── libmidi/        opencodewin/libmidi        UI 控件参考
├── nanotracker/    savannah-i-g/nanotracker   最小钢琴卷帘参考
├── tks/            bsp2/tks                   已评估，不用
├── studio/         本项目自建、已作废         见 studio/README.md
└── _archives/      下载的 zip 与零散资料（非仓库，含 llsid / WAV2VGM / adlib-pcm）
```

## `libvgm` 是唯一需要编译的

`cli/` 通过 `find_package(libvgm)` 链接它的三个静态库。移动目录后必须重新配置：

```bash
export PATH="/d/msys64/mingw64/bin:$PATH"
cmake -S refs/libvgm -B refs/libvgm/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_LIBAUDIO=OFF -DBUILD_PLAYER=OFF -DBUILD_VGM2WAV=ON -DUSE_SANITIZERS=OFF
cmake --build refs/libvgm/build
```

**`BUILD_PLAYER` 必须 OFF**：`player.cpp:1334` 用了 `WavWrt_SetFileName`，
那个符号在关掉 `BUILD_LIBAUDIO` 时不存在，编不过。`BUILD_VGM2WAV=ON` 才出
`vgm2wav.exe`（探针脚本要它渲染 wav）。

产物：`refs/libvgm/build/bin/libvgm-{emu,player,utils}.a` + `vgm2wav.exe`。

> 导出的 `libvgmTargets.cmake` 里写死了绝对路径，所以**挪动目录后不能复用旧的 build 目录**，
> 必须删掉重配。
