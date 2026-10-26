# アニメーション名

Warcraft III は、アニメーションを**名前**で見つけます。攻撃するユニットは、*Attack* で始まるアニメーションを再生します。そのため、シーケンスの名前は大切です。

名前には**主な言葉**があり、**追加の言葉**を付けることもできます。例：`Stand`、`Stand Ready`、`Attack Slam`、`Walk Fast`、`Stand - 2`。

## 主な言葉

| 名前 | ゲームが再生するとき |
|---|---|
| Stand | ユニットが何もしていないとき。 |
| Walk | ユニットが動くとき。 |
| Attack | ユニットが攻撃するとき。 |
| Spell | ユニットが呪文を唱えるとき。 |
| Death | ユニットが死ぬとき。 |
| Decay | 死体が朽ちていくとき（*Decay Flesh*、次に *Decay Bone*）。 |
| Birth | 建物が建てられるとき、またはユニットが現れるとき。 |
| Dissipate | 召喚されたユニットや霊のユニットが、死ぬときに消えていくとき。 |
| Portrait | ポートレートウィンドウの顔。*Portrait Talk* は、ユニットが話している間に再生されます。 |
| Morph | ユニットが姿を変えるとき。 |
| Sleep | ユニットが眠っているとき。 |
| Cinematic | シネマティックとマップだけで使います。 |

## 追加の言葉

| 言葉 | 意味 |
|---|---|
| Ready | 戦う準備ができた状態。たとえば *Stand Ready*。 |
| Slam | 強い攻撃。たとえば *Attack Slam* や *Spell Slam*。 |
| Victory | 戦いに勝った後。 |
| Hit | ユニットが攻撃を受けたとき。 |
| Channel | 時間のかかる呪文。 |
| Throw | 何かを投げる。 |
| Fast | 速い版。たとえば *Walk Fast*。 |
| Alternate | ユニットの 2 つ目の姿。たとえば *Metamorphosis* の後。 |
| Upgrade First … Fifth | 1 回目から 5 回目のアップグレードの後の建物。 |
| Work, Gold, Lumber | 労働ユニットのアニメーション。 |
| Swim | 水の中を動く。 |
| Defend | *Defend* がオンのとき。 |
| Talk | 話す（*Portrait* と一緒に使います）。 |

## 同じ名前の複数のアニメーション

同じ主な言葉のアニメーションを複数持てます。たとえば `Stand - 1`、`Stand - 2`、`Stand - 3` です。ゲームはその中から 1 つを偶然で選びます。[[t:menu_sequence_manager]] の**レアリティ**の値は、ゲームがそのアニメーションを選ぶ頻度を決めます。値が大きいほど、選ばれる回数が少なくなります。

## 使いやすい長さ

[[t:menu_sequence_manager]] には、次の長さのプリセットがあります（1 秒 30 フレームの場合）。

| プリセット | フレーム |
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
    *Death*、*Decay*、*Birth*、*Dissipate* は 1 回だけ再生されます。シーケンスマネージャーで **ループなし** に設定してください。
