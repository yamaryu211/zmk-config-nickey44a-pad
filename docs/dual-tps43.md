# Nickey44A-PAD のキーとトラックパッド

## キーの対応

移植元は [torabo-tsuki LP の `yamaryu211_main`（e45ba15）](https://github.com/yamaryu211/zmk-keyboard-torabo-tsuki-lp/blob/e45ba157f6ef610b51c636ebd35506437945f9b1/config/keymap.keymap) の S 配列。上3段は Q〜T・Y〜P の列を基準に合わせた。Bの右とNの左、右親指列の右端は全レイヤーで無割り当て。torabo の3段目中央2キーは除外した。

| 外側の列 | 左手 | 右手 |
|---|---|---|
| 上段 | Esc | Backspace |
| 中段 | 左Command（Mac）／左Control（Windows） | Enter |
| 下段・短押し | 言語2 | 言語1 |
| 下段・長押し | 使用中のOSの数字レイヤー | 使用中のOSのファンクションレイヤー |

| 最下段を左から | 左1 | 左2 | 左3 | 右1 | 右2 | 右3 |
|---|---|---|---|---|---|---|
| タップ | Alt | Delete | Space | Enter | Backspace | 空白 |
| 長押し | Alt | Ctrl（Macは⌘） | Shift | 数字 | 記号 | 空白 |

torabo の最下段12キーのうち、中央寄りの5キー（位置35〜39）を採用した。コンボは S+D で Tab、D+F で Shift+Tab、Q+W で Esc、A+S で Caps Lock、J+K またはコンマ＋ピリオドで中クリック。

自動マウスレイヤー（AML）は初期設定でオフ。トラックパッドを動かした直後も通常配列で入力できる。以前の動作に戻すには、`config/nickey_layers.h` の `NICKEY_AUTO_MOUSE_LAYER` を `1` にして再ビルドする。左右のパッドとマウスキーからの自動切替が有効になり、AML中はJ・コンマが左クリック、K・ピリオドが右クリックになる。

親指列から外れた言語キーと設定操作には次の経路がある。

| 操作 | 結果 |
|---|---|
| Enterを保持し、Tをタップ／保持 | 言語1／ファンクションレイヤー |
| Enterを保持し、Gをタップ | 言語2 |
| Enterを保持してからBackspaceも保持 | キーボード設定レイヤー |
| 設定で H / J / K / L / 右隣のキー | BT 0 Windows / 1 Mac / 2 iPad / 3 iPhone / 4 iPad mini |
| 設定で W / E | 現在の接続先にWindows / Mac配列を保存。USBでも使用可 |
| 設定で Q / R | Studioロック解除 / USB・BLE出力切替 |

## 左右共通のジェスチャー

| 指の操作 | 動作 |
|---|---|
| 1本指で移動／タップ | カーソル移動／左クリック |
| 2本指で上下／左右に移動 | 縦／横スクロール。下に動かすとページの上へ、上に動かすとページの下へ移動。縦方向は指を離すと慣性あり |
| 2本指でタップ | 右クリック |
| ピンチイン／アウト | 対応アプリで縮小／拡大。WindowsはCtrl、Macは⌘と、マイナス／イコールの組み合わせを送信 |

Windowsは標準のマウスホイール方向、Macは「マウス → ナチュラルなスクロール」がオンの状態を前提とする。Mac配列では縦・横のホイールの符号を反転する。配列に連動するため、toraboのレイヤー16による手動の方向フラグは不要。数字・ファンクションなどのレイヤーでもパッドの基本操作は共通。[Appleのマウス設定](https://support.apple.com/ja-jp/guide/mac-help/mh29222/mac)

TPS43はピンチを符号付きの相対値で通知する。1本指のエッジスクロールは未実装。固定したドライバーは指の絶対座標を入力イベントとして公開しておらず、端から触れたかを判定するにはドライバーの拡張が必要。[TPS43ドライバー](https://github.com/geeksville/zmk_driver_azoteq/blob/66d510249d139569a4060c35bd78c6a2159f83ed/drivers/input/tps43.c)

## 配線と検証

両側とも SDA は P0.09、SCL は P1.01、RDY は P0.15、NRST は P0.19。I²Cアドレスは各MCUのバス上で `0x74`。左はイベントをそのまま BLE split の reg 0 で右へ送り、右centralで両パッドに同じ向き補正とOS別の変換を適用する。慣性の状態はパッドごとに独立している。

左右のUF2をビルドした後、生成されたDevicetreeとKconfigを次のコマンドで確認できる。

```sh
python3 scripts/check_dual_tps43.py --zephyr-base "$ZEPHYR_BASE" \
    --left-build /path/to/left-build --right-build /path/to/right-build
```

実機では、左右それぞれのカーソルの向き、Windows・Macでの縦横スクロール、ピンチとタップの感度を確認する。左パッドの取り付け向きには右パッドと同じ向きを仮定した。Studioに以前のキーマップを保存している場合は、新しい割り当てを試す前にStudioのキーマップをファームウェア既定値へ戻す。
