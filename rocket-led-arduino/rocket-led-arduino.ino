// Arduino Nano + WS2812B
// ---------------------------------------------
// A0: Brightness  明るさ（RGBを直接スケーリング。setBrightnessは不使用）
// A1: Speed       速度ノブ（Segment Aの“流れ”と、Segment Bの“フェーズ切替”の両方に効く）
//
// Segment A (LED 0..32):
//   - パターン: "WWWBBB"（6個周期）
//   - 動き   : ベルトのように流れる（= チェイス）
//   - 方向   : REVERSE_FLOW=1 のとき“左流れ”。0 のとき“右流れ”
//   - 速度   : chaseDelayMs（小さいほど速い）
//
// Segment B (LED 33..107):
//   - 挙動   : 9つのフェーズを順番に前進（Phase01→Phase09）
//   - 表示   : 現在のフェーズ範囲だけ点灯（他は消灯）
//   - 特例   : Phase06 の LED 74 だけオレンジで点灯
//   - 速度   : phaseDelayMs（長いほどゆっくり）
//
// Speedノブの関係:
//   - phaseDelayMs = A1 を 5.0s..0.3s にマップ（ゆっくり→速い）
//   - chaseDelayMs = phaseDelayMs / 8 を目安に決定し、
//                    CHASE_DELAY_MIN_MS..MAX_MS でクランプ（安全な範囲に制限）
// ---------------------------------------------

#include <Adafruit_NeoPixel.h>

// ===== 配線・基本設定 =====
const int DIN_PIN    = 1;     // 信号ピン（D1）
const int LED_COUNT  = 108;   // 全LED数（インデックス 0..107）

// Segment A の“流れ”方向だけを反転（Segment Bのフェーズには影響しない）
#define REVERSE_FLOW 1        // 0: 右流れ, 1: 左流れ（Aのみ反転）

// ===== ポテンショ（可変抵抗） =====
const int BRIGHT_POT = A0;    // 明るさ入力
const int SPEED_POT  = A1;    // 速度入力（AとBの両方に影響）

Adafruit_NeoPixel pixels(LED_COUNT, DIN_PIN, NEO_GRB + NEO_KHZ800);

// ===== セグメントの範囲 =====
const int SEG_A_START = 0;
const int SEG_A_END   = 32;    // 0..32 が Segment A
const int SEG_B_START = 33;
const int SEG_B_END   = 107;   // 33..107 が Segment B

// ===== Segment B のフェーズ範囲（各フェーズは連続区間・いずれか1つだけ点灯）=====
struct Range { int s, e; };
Range PHASES[] = {
  {33, 41},   // Phase 01
  {42, 45},   // Phase 02
  {46, 57},   // Phase 03
  {58, 62},   // Phase 04
  {63, 73},   // Phase 05
  {74, 79},   // Phase 06（LED 74 だけオレンジ）
  {80, 87},   // Phase 07
  {88, 101},  // Phase 08
  {102, 107}  // Phase 09
};
const int PHASE_COUNT = sizeof(PHASES)/sizeof(PHASES[0]);

// ===== ランタイム状態（時間で進行） =====
int chaseStep    = 0;    // Segment A の "WWWBBB" 位置（0..5）
int phaseIndex   = 0;    // Segment B の 現在フェーズ（0..8）
unsigned long lastChase  = 0;  // 最後にチェイスを進めた時刻
unsigned long lastPhase  = 0;  // 最後にフェーズを進めた時刻

// ===== 速度（A1）から毎フレーム更新される実際の遅延 =====
unsigned long chaseDelayMs = 100;  // Segment A の“流れ”速度（初期値; ループで上書き）
unsigned long phaseDelayMs = 1200; // Segment B のフェーズ滞在時間（初期値; ループで上書き）

// ======= ユーザが調整しやすい“外出し”定数（下限・上限） =======
// 明るさの最小/最大（直接RGBスケーリング用）
// 目視できない暗さを避けるために下限を持たせる
const uint8_t BRIGHT_MIN = 10;
const uint8_t BRIGHT_MAX = 255;

// Segment A のチェイス速度の下限/上限（ms）— 数値が小さいほど速い
// ※ フェーズ速度（phaseDelayMs）の下限/上限ではない
const unsigned long CHASE_DELAY_MIN_MS = 20;   // とても速い
const unsigned long CHASE_DELAY_MAX_MS = 300;  // ゆっくり
// ===============================================================

// ===== 色ヘルパ（b=0..255 の明るさスケール） =====
static inline uint32_t colWhite(uint8_t b){ return pixels.Color(b, b, b); }
static inline uint32_t colBlue (uint8_t b){ return pixels.Color(b/7, b/7, b); } // 白より青を強調
static inline uint32_t colOrange(uint8_t b){ return pixels.Color(b, b/3, 0); }  // オレンジ
static const uint32_t COL_OFF = 0;  // 消灯

void setup(){
  pixels.begin();   // ライブラリ初期化
  pixels.clear();   // 全消灯（バッファ）
  pixels.show();    // 実LEDへ反映
}

void loop(){
  // ---- 入力を読む（A0:明るさ / A1:速度） ----
  int rawB = analogRead(BRIGHT_POT); // 0..1023（0V..5V）
  int rawS = analogRead(SPEED_POT);  // 0..1023

  // ---- 明るさ：0..255 相当に縮小し、下限/上限でクランプ ----
  int b = rawB / 4;                  // 0..255 目安
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > BRIGHT_MAX) b = BRIGHT_MAX;

  // ---- 速度ノブ → 遅延へ変換 ----
  // Segment B のフェーズ切替時間を 5.0s..0.3s へマップ（大きく回すほど速く）
  phaseDelayMs = map(rawS, 0, 1023, 5000, 300);

  // Segment A のチェイスは フェーズの約1/8 で速めにし、
  // さらに“安全範囲”にクランプ（極端な速すぎ/遅すぎを防止）
  unsigned long cd = phaseDelayMs / 8;
  if (cd < CHASE_DELAY_MIN_MS) cd = CHASE_DELAY_MIN_MS;
  if (cd > CHASE_DELAY_MAX_MS) cd = CHASE_DELAY_MAX_MS;
  chaseDelayMs = cd;

  unsigned long now = millis();

  // ---- 経過時間で進行（非ブロッキング）----
  if (now - lastChase >= chaseDelayMs){
    lastChase = now;
    chaseStep = (chaseStep + 1) % 6;   // A の "WWWBBB" を1ステップ進める（方向は描画側で処理）
  }
  if (now - lastPhase >= phaseDelayMs){
    lastPhase = now;
    phaseIndex = (phaseIndex + 1) % PHASE_COUNT; // B のフェーズを1つ進める（常に前進）
  }

  // ---- 描画：Segment A（0..32）— "WWWBBB" が流れる ----
  for (int i = SEG_A_START; i <= SEG_A_END; i++){
    int rel = i - SEG_A_START;  // セグメント先頭からの相対位置

    // 方向反転はここで実施（REVERSE_FLOW=1 → 左流れ）
    int idx;
#if REVERSE_FLOW
    idx = ((rel - chaseStep) % 6 + 6) % 6;  // 負の剰余対策込み
#else
    idx = (rel + chaseStep) % 6;
#endif

    // idx 0..2 = White, 3..5 = Blue（= "WWWBBB"）
    uint32_t col = (idx < 3) ? colWhite((uint8_t)b) : colBlue((uint8_t)b);
    pixels.setPixelColor(i, col);
  }

  // ---- 描画：Segment B（33..107）— 現在フェーズ範囲のみ点灯 ----
  // まず全消灯（B領域）
  for (int i = SEG_B_START; i <= SEG_B_END; i++) {
    pixels.setPixelColor(i, COL_OFF);
  }

  // 現在のフェーズ範囲を点灯（Phase06のLED 74だけオレンジ）
  Range r = PHASES[phaseIndex];
  for (int i = r.s; i <= r.e; i++){
    if (phaseIndex == 5 && i == 74){          // Phase06 & LED 74 → オレンジ
      pixels.setPixelColor(i, colOrange((uint8_t)b));
    }else{
      pixels.setPixelColor(i, colWhite((uint8_t)b));
    }
  }

  // ---- 反映 & 軽い間引き ----
  pixels.show();   // バッファを実LEDへ出力
  delay(10);       // 小休止（安定性とCPU休憩）。タイミング制御は上の millis で実施
}
