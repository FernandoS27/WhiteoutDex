# 动画名称

魔兽争霸 III 通过 **名称** 找到动画。一个正在攻击的单位会播放以 *Attack* 开头的动画。所以你的序列名称很重要。

名称由一个 **主词** 组成，还可以带有 **附加词**。示例：`Stand`、`Stand Ready`、`Attack Slam`、`Walk Fast`、`Stand - 2`。

## 主词

| 名称 | 游戏什么时候播放它 |
|---|---|
| Stand | 单位什么也不做。 |
| Walk | 单位在移动。 |
| Attack | 单位在攻击。 |
| Spell | 单位在施法。 |
| Death | 单位死亡。 |
| Decay | 尸体腐烂消失（先 *Decay Flesh*，再 *Decay Bone*）。 |
| Birth | 建筑正在建造，或单位出现。 |
| Dissipate | 召唤单位或灵魂单位死亡时逐渐消散。 |
| Portrait | 头像窗口中的脸。单位说话时播放 *Portrait Talk*。 |
| Morph | 单位改变形态。 |
| Sleep | 单位在睡觉。 |
| Cinematic | 只用于过场动画和地图。 |

## 附加词

| 词 | 含义 |
|---|---|
| Ready | 准备战斗，例如 *Stand Ready*。 |
| Slam | 一次重击，例如 *Attack Slam* 或 *Spell Slam*。 |
| Victory | 战斗胜利之后。 |
| Hit | 单位被击中。 |
| Channel | 需要持续一段时间的法术。 |
| Throw | 投掷东西。 |
| Fast | 更快的版本，例如 *Walk Fast*。 |
| Alternate | 单位的第二种形态，例如 *Metamorphosis*（恶魔变形）之后。 |
| Upgrade First … Fifth | 建筑完成第 1 到第 5 次升级之后。 |
| Work, Gold, Lumber | 工人的动画。 |
| Swim | 在水中移动。 |
| Defend | 开启 *Defend*（防御）时。 |
| Talk | 说话（与 *Portrait* 一起使用）。 |

## 同名的多个动画

同一个主词可以有多个动画，例如 `Stand - 1`、`Stand - 2` 和 `Stand - 3`。游戏会随机选择一个。[[t:menu_sequence_manager]] 中的 **稀有度** 数值告诉游戏选择某个动画的频率：数值越高，选中得越少。

## 合适的长度

[[t:menu_sequence_manager]] 有以下长度的预设（每秒 30 帧）：

| 预设 | 帧 |
|---|---|
| Stand | 80 |
| Walk | 28 |
| Stand Ready | 20 |
| Attack | 24 |
| Attack Slam | 30 |
| Spell | 28 |
| Spell Slam | 24 |
| Death | 64 |
| Portrait Talk | 70 |

!!! note
    *Death*、*Decay*、*Birth* 和 *Dissipate* 只播放一次。请在序列管理器中把它们设为 **不循环**。
