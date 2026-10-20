# FaceFX

**FaceFX** 让重制版英雄说话时脸部动起来：嘴巴、眼睛和眉毛跟随语音变化。数据保存在一个 `.facefx_ingame` 文件中。FaceFX 对象把这个文件和模型关联起来。

用 **Create > Helpers > Warcraft III Plugins > Wc3 FaceFX** 创建它。FaceFX 只在 **重制版** 模型中有效。

[[shot:obj_facefx_r2|FaceFX 设置。]]

| 编号 | 作用 |
|---|---|
| [[ctl:9]] | 选择 `.facefx_ingame` 文件。 |
| [[ctl:10]] | 显示一段简短的说明。 |

两个字段保存 **目标名称**（游戏为这张脸使用的名称）和 **文件路径**。

!!! note
    导入器会把重制版模型中的 FaceFX 对象带入场景。导出器会原样写回它们。
