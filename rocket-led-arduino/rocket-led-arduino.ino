// Arduino Nano + WS2812B
// ---------------------------------------------
// A0: Brightness（RGB直スケール / setBrightness不使用）
// A1: Speed（Segment Aの流れ & Segment Bのフェーズ切替）
//
// Segment A (0..32):
//   PATTERN_BLOCK=3 → "WWWBBB" / =2 → "WWBB"
//   REVERSE_FLOW=1 で左流れ（Aのみ反転）
//
// Segment B (33..107):
//   Phase01→Phase09 → Phase10(全消灯) → Phase01…
//   Phase06 は LED74 をオレンジ
//
// 速度関係:
//   phaseDelayMs = A1 を 5.0s..0.3s へマップ
//   chaseDelayMs = phaseDelayMs / 8 を基準に MIN..MAX でクランプ
// ---------------------------------------------

#include <Adafruit_NeoPixel.h>

// ===== 配線・基本設定 =====
const int DIN_PIN    = 1;     // D1
const int LED_COUNT  = 108;   // 0..107

// Segment A の流れ方向（Bのフェーズには影響しない）
#define REVERSE_FLOW 1        // 0:右流れ, 1:左流れ

// ===== パターン切替（2=WWBB, 3=WWWBBB）=====
const int PATTERN_BLOCK = 2;                  // 2 か 3 を選ぶ
const int PATTERN_PERIOD = PATTERN_BLOCK * 2; // 周期

// ===== ポテンショ入力 =====
const int BRIGHT_POT = A0;    // 明るさ
const int SPEED_POT  = A1;    // 速度

Adafruit_NeoPixel pixels(LED_COUNT, DIN_PIN, NEO_GRB + NEO_KHZ800);

// ===== セグメント範囲 =====
const int SEG_A_START = 0;
const int SEG_A_END   = 32;
const int SEG_B_START = 33;
const int SEG_B_END   = 107;

// ===== Segment B のフェーズ範囲（01..09）。Phase10 は全消灯 =====
struct Range { int s, e; };
Range PHASES[] = {
  {33, 41},   // Phase 01
  {42, 45},   // Phase 02
  {46, 57},   // Phase 03
  {58, 62},   // Phase 04
  {63, 73},   // Phase 05
  {74, 79},   // Phase 06（LED74=オレンジ）
  {80, 87},   // Phase 07
  {88, 101},  // Phase 08
  {102, 107}  // Phase 09
};
const int PHASE_COUNT = 10;   // 01..09 + 10(全消灯)

// ===== ランタイム状態 =====
int chaseStep    = 0;                 // 0..PATTERN_PERIOD-1
int phaseIndex   = 0;                 // 0..9（0=Phase01, 9=Phase10）
unsigned long lastChase = 0;
unsigned long lastPhase = 0;

// ===== 動作遅延（A1から毎フレーム再計算）=====
unsigned long chaseDelayMs = 100;
unsigned long phaseDelayMs = 1200;

// ===== “外出し”定数 =====
const uint8_t BRIGHT_MIN = 10;
const uint8_t BRIGHT_MAX = 255;
const unsigned long CHASE_DELAY_MIN_MS = 20;
const unsigned long CHASE_DELAY_MAX_MS = 300;

// ===== 色ヘルパ =====
static inline uint32_t colWhite(uint8_t b){ return pixels.Color(b, b, b); }
static inline uint32_t colBlue (uint8_t b){ return pixels.Color(b/7, b/7, b); }
static inline uint32_t colOrange(uint8_t b){ return pixels.Color(b, b/3, 0); }
static const uint32_t COL_OFF = 0;

void setup(){
  pixels.begin();
  pixels.clear();
  pixels.show();
}

void loop(){
  // ---- 入力取得 ----
  int rawB = analogRead(BRIGHT_POT); // 0..1023
  int rawS = analogRead(SPEED_POT);  // 0..1023

  // ---- 明るさ（0..255 クランプ）----
  int b = rawB / 4;
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > BRIGHT_MAX) b = BRIGHT_MAX;

  // ---- 速度 → 遅延 ----
  phaseDelayMs = map(rawS, 0, 1023, 5000, 300);
  unsigned long cd = phaseDelayMs / 8;
  if (cd < CHASE_DELAY_MIN_MS) cd = CHASE_DELAY_MIN_MS;
  if (cd > CHASE_DELAY_MAX_MS) cd = CHASE_DELAY_MAX_MS;
  chaseDelayMs = cd;

  unsigned long now = millis();

  // ---- 進行（非ブロッキング）----
  if (now - lastChase >= chaseDelayMs){
    lastChase = now;
    chaseStep = (chaseStep + 1) % PATTERN_PERIOD;
  }
  if (now - lastPhase >= phaseDelayMs){
    lastPhase = now;
    phaseIndex = (phaseIndex + 1) % PHASE_COUNT; // 0..9
  }

  // ---- 描画：Segment A（流れパターン）----
  for (int i = SEG_A_START; i <= SEG_A_END; i++){
    int rel = i - SEG_A_START;
    int idx;
#if REVERSE_FLOW
    idx = ((rel - chaseStep) % PATTERN_PERIOD + PATTERN_PERIOD) % PATTERN_PERIOD;
#else
    idx = (rel + chaseStep) % PATTERN_PERIOD;
#endif
    uint32_t col = (idx < PATTERN_BLOCK) ? colWhite((uint8_t)b) : colBlue((uint8_t)b);
    pixels.setPixelColor(i, col);
  }

  // ---- 描画：Segment B（フェーズ）----
  // いったんB領域は全消灯
  for (int i = SEG_B_START; i <= SEG_B_END; i++){
    pixels.setPixelColor(i, COL_OFF);
  }

  // phaseIndex 0..8 → Phase01..09 を点灯、9 → Phase10(全消灯)
  if (phaseIndex < 9){
    Range r = PHASES[phaseIndex];
    for (int i = r.s; i <= r.e; i++){
      if (phaseIndex == 5 && i == 74){ // Phase06 & LED74
        pixels.setPixelColor(i, colOrange((uint8_t)b));
      } else {
        pixels.setPixelColor(i, colWhite((uint8_t)b));
      }
    }
  }
  // phaseIndex==9 のときは何も点けない（全消灯のまま）

  // ---- 反映 ----
  pixels.show();
  delay(10); // 軽い休止（タイミング制御は上のmillis）
}
