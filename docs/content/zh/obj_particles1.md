# 粒子发射器 1

粒子发射器 1 发射的不是图片，而是 **小模型**。用它制作落叶、石块或箭矢这类本身就是模型的东西。

用 **Create > Geometry > Particle Systems > Wc3Particles1** 创建它。

## Particle Configuration

[[shot:obj_particles1_r1|Particle Configuration（粒子配置）。]]

| 编号 | 作用 |
|---|---|
| [[ctl:2020]] | 从 `.ini` 文件加载所有设置。 |
| [[ctl:2021]] | 把所有设置保存到 `.ini` 文件。 |
| [[ctl:2022]] | 开启时，*导入* 也会加载数值的动画关键帧。 |

## Emitter Options

[[shot:obj_particles1_r2|Emitter Options（发射器选项）。]]

| 编号 | 作用 |
|---|---|
| [[ctl:5002]] | 模型飞出的速度。 |
| [[ctl:5004]] | 方向的上下角度。 |
| [[ctl:5006]] | 方向的侧向角度。 |
| [[ctl:5008]] | 把模型往下拉。 |
| [[ctl:2030]] | **控制器：** 动画数值在关键帧之间如何变化。每个数值都有自己的控制器。 |

## Timing Options

[[shot:obj_particles1_r3|Timing Options（时间选项）。]]

| 编号 | 作用 |
|---|---|
| [[ctl:5010]] | 一个模型存在多少秒。 |
| [[ctl:5012]] | 每秒发射多少个模型。 |

## Model Options

[[shot:obj_particles1_r4|Model Options（模型选项）。]]

| 编号 | 作用 |
|---|---|
| [[ctl:5017]] | 模型在游戏中的文件夹。 |
| [[ctl:5016]] | 模型的文件名，例如 `Abilities\Weapons\Arrow\ArrowMissile.mdl`。 |
| [[ctl:2024]] | 从游戏档案中选择一个模型。 |
| [[ctl:2023]] | 选择磁盘上的一个模型文件。 |
| [[ctl:5014]] | 视口预览中模型的大小。 |

!!! note
    经典版模型在这个路径中常常写扩展名 `.mdl`，即使游戏中的文件是 `.mdx`。游戏两者都能找到。
