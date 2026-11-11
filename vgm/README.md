# 测试用 VGM

这里的 `.vgm` 文件是**商业游戏的原声**（Spindizzy II / Sharp X68000），
**不随仓库分发**（`.gitignore` 里排除了 `vgm/**/*.vgm`）。请自行准备。

## 当前用于验证的曲目

`31 Grass Maze.vgm` —— 选它的原因：

| 项 | 值 |
|---|---|
| 芯片 | YM2151（OPM）×1，4 MHz |
| 总长 | 2,406,316 采样（54.6 s @44100） |
| note 数 | 2515（8 通道） |
| 特征 | 8 通道全用、ALG 覆盖 0/2/3/5 四类、载波 ML 有 1/2/4/8/10 五种 |

它是本项目的**唯一测试曲**（见 `docs/IMPLEMENTATION-CLI.md`）。

## 需要什么

把 `31 Grass Maze.vgm` 放到这个目录下（路径保持 `vgm/Spindizzy_II_(Sharp_X68000)/31 Grass Maze.vgm`），
README 里的命令即可直接跑。换别的曲子也行，只要改一下命令里的路径。

## 格式要求

- 标准 VGM 1.5x（**非** `.vgz` 压缩包）
- 单芯片或含 YM2151 的多芯片文件都能导入；非 YM2151 的写会完整落到 `orphans`，不丢
- 立体声/单声道均可
