#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

extern "C" {
  #include "driver/dac_continuous.h"
  #include "esp_err.h"
}

// =====================================================
// GERADOR DE AUDIO
// ESP32 DEVKIT V1 / ESP32-WROOM-32
//
// MODOS:
//   SENO
//   TRIANGULAR
//   QUADRADA
//   SERRA
//   RUIDO BRANCO
//   RUIDO ROSA
//
// CONTROLES:
//   GPIO32 = MODO
//   GPIO33 = AMPLITUDE
//   GPIO27 = OUTPUT ON/OFF
//   GPIO18 = FREQUENCIA -
//   GPIO19 = FREQUENCIA +
//
// TFT ST7789 1.9" (170x320, retrato como no video):
//   SCLK = GPIO14
//   MOSI = GPIO13
//   CS   = GPIO23
//   DC   = GPIO16
//   RST  = GPIO17
//   BL   = GPIO26 (desliga apos 30 s sem uso)
//
// AUDIO:
//   GPIO25 = DAC0
//
// Display usa Adafruit_GFX + Adafruit_ST7789 com HSPI de hardware.
// A frequencia e controlada por dois botoes: GPIO18 (-) e GPIO19 (+).
// =====================================================


// =====================================================
// PINOS
// =====================================================

#define TFT_SCLK      14
#define TFT_MOSI      13
#define TFT_CS        23
#define TFT_DC        16
#define TFT_RST       17
#define TFT_BL        26

#define BACKLIGHT_TIMEOUT 60000UL
#define TFT_BL_ON     HIGH
#define TFT_BL_OFF    LOW

#define TFT_WIDTH     170
#define TFT_HEIGHT    320

#define BTN_MODE      32
#define BTN_AMP       33
#define BTN_OUTPUT    27
#define BTN_FREQ_DOWN 18
#define BTN_FREQ_UP   19

#define AUDIO_PIN     25


// =====================================================
// AUDIO
// =====================================================

// Taxa alta para melhorar as formas de onda,
// principalmente nas frequencias mais altas.

#define SAMPLE_RATE       192000
#define AUDIO_BUFFER_SIZE 384

uint8_t audioBuffer[AUDIO_BUFFER_SIZE];

dac_continuous_handle_t dacHandle = NULL;


// =====================================================
// MODOS
// =====================================================

enum {
  MODE_SINE = 0,
  MODE_TRIANGLE,
  MODE_SQUARE,
  MODE_SAW,
  MODE_WHITE,
  MODE_PINK,
  MODE_TOTAL
};

volatile uint8_t currentMode = MODE_SINE;

const char *modeNames[] = {
  "SENO",
  "TRIANGULAR",
  "QUADRADA",
  "SERRA",
  "RUIDO BRANCO",
  "RUIDO ROSA"
};


// =====================================================
// FREQUENCIA
// =====================================================

const uint32_t FREQ_MIN = 20;
const uint32_t FREQ_MAX = 20000;

volatile uint32_t controlHz = 1000;


// =====================================================
// AMPLITUDE
// =====================================================

const uint8_t amplitudeTable[] = {
  10,
  25,
  50,
  75,
  100
};

#define AMP_COUNT 5

volatile uint8_t amplitudeIndex = 4;


// =====================================================
// OUTPUT
// =====================================================

volatile bool outputEnabled = true;


// =====================================================
// PEDIDO DE RESET DO GERADOR
// =====================================================

volatile bool resetAudioStateRequested = false;


// =====================================================
// DDS
// =====================================================

uint32_t phaseAccumulator = 0;


// =====================================================
// TABELA SENO
// =====================================================

int16_t sineTable[256];


// =====================================================
// RANDOM
// =====================================================

uint32_t randomState = 0xA5366B4D;


// =====================================================
// RUIDO ROSA
// =====================================================

#define PINK_ROWS 12

int16_t pinkRows[PINK_ROWS];

int32_t pinkSum = 0;

uint32_t pinkCounter = 0;


// =====================================================
// FILTRO PARA OS RUIDOS
// =====================================================

float noiseFilterState = 0.0f;


// =====================================================
// BOTOES
// =====================================================

#define BUTTON_COUNT 5

const uint8_t buttonPins[BUTTON_COUNT] = {
  BTN_MODE,       // 0
  BTN_AMP,        // 1
  BTN_OUTPUT,     // 2
  BTN_FREQ_DOWN,  // 3
  BTN_FREQ_UP     // 4
};

bool buttonStable[BUTTON_COUNT];
bool buttonLastRead[BUTTON_COUNT];

uint32_t buttonChangeTime[BUTTON_COUNT];
uint32_t buttonPressTime[BUTTON_COUNT];
uint32_t buttonNextRepeat[BUTTON_COUNT];


// Eventos dos botoes

#define EVENT_NONE   0
#define EVENT_PRESS  1
#define EVENT_REPEAT 2


// =====================================================
// TFT ST7789 1.9" - SPI DE HARDWARE (HSPI)
// =====================================================
//
// IMPORTANTE:
// Esta versao usa o periférico HSPI do ESP32 em vez de SPI por software.
// Isso deixa as atualizacoes do display muito mais rapidas e mantem
// GPIO18/GPIO19 livres para os botoes de frequencia.
//
// Ligacoes:
//   TFT SCL/SCK -> GPIO14
//   TFT SDA/MOSI -> GPIO13
//   TFT CS       -> GPIO23
//   TFT DC       -> GPIO16
//   TFT RST      -> GPIO17
//   TFT VCC      -> 3V3
//   TFT GND      -> GND
//   TFT BL       -> GPIO26
//
// Resolucao assumida: 170 x 320.
// Rotacao 0 = retrato.
// =====================================================

SPIClass displaySPI(HSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(
  &displaySPI,
  TFT_CS,
  TFT_DC,
  TFT_RST
);

bool lcdAvailable = false;

// Cache da interface. O TFT so redesenha o que realmente mudou.
int16_t uiLastMode = -1;
uint32_t uiLastFrequency = 0xFFFFFFFFUL;
int16_t uiLastAmplitude = -1;
int16_t uiLastOutput = -1;
bool uiScreenReady = false;

// Estado independente apenas para animacao dos graficos de ruido.
uint32_t uiNoiseState = 0x6D2B79F5;
uint32_t lastNoiseGraphUpdate = 0;

// =====================================================
// BACKLIGHT
// =====================================================

bool backlightOn = true;
uint32_t lastUserActivity = 0;

void setBacklight(bool on) {

  backlightOn = on;

  digitalWrite(
    TFT_BL,
    on ? TFT_BL_ON : TFT_BL_OFF
  );
}

void registerUserActivity() {

  lastUserActivity = millis();

  if (!backlightOn) {
    setBacklight(true);
  }
}

void updateBacklightTimeout() {

  if (
    backlightOn &&
    (uint32_t)(millis() - lastUserActivity) >= BACKLIGHT_TIMEOUT
  ) {

    setBacklight(false);
  }
}


// =====================================================
// INICIALIZAR TFT
// =====================================================

void lcdInit() {

  // HSPI com os mesmos pinos fisicos usados anteriormente.
  // MISO nao e necessario para o ST7789.
  displaySPI.begin(
    TFT_SCLK,
    -1,
    TFT_MOSI,
    TFT_CS
  );

  tft.init(
    TFT_WIDTH,
    TFT_HEIGHT
  );

  tft.setRotation(0);

  // Muitos modulos ST7789 1.9" precisam de inversao.
  // Se as cores ficarem estranhas, teste false.
  tft.invertDisplay(true);

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  lcdAvailable = true;
  uiScreenReady = false;
}


// =====================================================
// LIMPAR TFT
// =====================================================

void lcdClear() {

  if (!lcdAvailable) {
    return;
  }

  tft.fillScreen(ST77XX_BLACK);
  uiScreenReady = false;
}


// =====================================================
// TEXTO CENTRALIZADO
// =====================================================

void drawCenteredText(
  const char *text,
  int16_t y,
  uint8_t textSize,
  uint16_t color
) {

  int16_t x1, y1;
  uint16_t w, h;

  tft.setTextSize(textSize);
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);

  int16_t x = (tft.width() - (int16_t)w) / 2;
  if (x < 0) x = 0;

  tft.setTextColor(color, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.print(text);
}


// =====================================================
// LINHA DE TEXTO PARA SPLASH/ERROS
// =====================================================

void lcdPrintLine(
  uint8_t row,
  const char *text
) {

  if (!lcdAvailable) {
    return;
  }

  int16_t y = (row == 0) ? 105 : 132;

  tft.fillRect(0, y - 2, tft.width(), 22, ST77XX_BLACK);
  drawCenteredText(text, y, 1, ST77XX_WHITE);
}


// =====================================================
// CABECALHO DO TFT - ESTILO DO VIDEO
// =====================================================

void drawDisplayHeader() {

  if (!lcdAvailable) {
    return;
  }

  // Moldura escura + faixa ciano/azul.
  tft.fillRect(7, 27, 156, 27, ST77XX_BLUE);
  tft.drawRect(6, 26, 158, 29, ST77XX_CYAN);

  drawCenteredText(
    "GERADOR AUDIO",
    33,
    2,
    ST77XX_WHITE
  );
}


// =====================================================
// RANDOM EXCLUSIVO DA INTERFACE
// =====================================================

uint32_t uiFastRandom32() {

  uint32_t x = uiNoiseState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  uiNoiseState = x;
  return x;
}


// =====================================================
// GRAFICO DA FORMA DE ONDA
// =====================================================

void drawWaveformPreview(uint8_t mode) {

  const int16_t x0 = 19;
  const int16_t y0 = 244;
  const int16_t w  = 132;
  const int16_t h  = 50;
  const int16_t mid = y0 + h / 2;

  // Limpa somente a area do grafico.
  tft.fillRect(x0 - 2, y0 - 3, w + 4, h + 7, ST77XX_BLACK);

  // Linhas azuladas como no video.
  tft.drawLine(x0, y0, x0 + w - 1, y0, ST77XX_BLUE);
  tft.drawLine(x0, y0 + h, x0 + w - 1, y0 + h, ST77XX_BLUE);

  uint16_t waveColor = ST77XX_CYAN;

  if (mode == MODE_SINE) {

    int16_t oldX = x0;
    int16_t oldY = mid;

    // Usa a tabela seno ja existente do gerador, evitando sinf() na interface.
    for (int16_t i = 1; i < w; i++) {
      uint8_t tableIndex = (uint8_t)(((uint32_t)i * 255UL) / (uint32_t)(w - 1));
      int16_t y = mid - (int16_t)(((int32_t)sineTable[tableIndex] * 18) / 127);
      int16_t x = x0 + i;
      tft.drawLine(oldX, oldY, x, y, waveColor);
      oldX = x;
      oldY = y;
    }
  }

  else if (mode == MODE_TRIANGLE) {

    int16_t q = w / 4;
    tft.drawLine(x0,           mid,      x0 + q,       y0 + 7,     waveColor);
    tft.drawLine(x0 + q,       y0 + 7,   x0 + 3 * q,   y0 + h - 7, waveColor);
    tft.drawLine(x0 + 3 * q,   y0 + h-7, x0 + w - 1,   mid,        waveColor);
  }

  else if (mode == MODE_SQUARE) {

    int16_t top = y0 + 9;
    int16_t bot = y0 + h - 9;
    int16_t q = w / 4;

    tft.drawLine(x0, top, x0 + q, top, waveColor);
    tft.drawLine(x0 + q, top, x0 + q, bot, waveColor);
    tft.drawLine(x0 + q, bot, x0 + 3*q, bot, waveColor);
    tft.drawLine(x0 + 3*q, bot, x0 + 3*q, top, waveColor);
    tft.drawLine(x0 + 3*q, top, x0 + w - 1, top, waveColor);
  }

  else if (mode == MODE_SAW) {

    int16_t top = y0 + 8;
    int16_t bot = y0 + h - 8;
    int16_t half = w / 2;

    tft.drawLine(x0, bot, x0 + half - 1, top, waveColor);
    tft.drawLine(x0 + half - 1, top, x0 + half - 1, bot, waveColor);
    tft.drawLine(x0 + half - 1, bot, x0 + w - 1, top, waveColor);
  }

  else if (mode == MODE_WHITE) {

    int16_t oldX = x0;
    int16_t oldY = mid;

    for (int16_t i = 3; i < w; i += 3) {
      int16_t rnd = (int16_t)((uiFastRandom32() >> 24) & 0xFF) - 128;
      int16_t y = mid + (rnd * 19) / 128;
      int16_t x = x0 + i;
      tft.drawLine(oldX, oldY, x, y, waveColor);
      oldX = x;
      oldY = y;
    }
  }

  else if (mode == MODE_PINK) {

    int16_t oldX = x0;
    int16_t oldY = mid;
    int32_t smooth = 0;

    for (int16_t i = 3; i < w; i += 3) {
      int16_t rnd = (int16_t)((uiFastRandom32() >> 24) & 0xFF) - 128;
      smooth = (smooth * 3 + rnd) / 4;
      int16_t y = mid + (smooth * 21) / 128;
      int16_t x = x0 + i;
      tft.drawLine(oldX, oldY, x, y, waveColor);
      oldX = x;
      oldY = y;
    }
  }
}


// =====================================================
// ANIMAR APENAS OS GRAFICOS DE RUIDO
// =====================================================

void updateNoiseGraphAnimation() {

  if (!lcdAvailable || !backlightOn) {
    return;
  }

  uint8_t mode = currentMode;

  if (mode != MODE_WHITE && mode != MODE_PINK) {
    return;
  }

  uint32_t now = millis();

  // Enquanto o usuario esta apertando/segurando botoes,
  // a prioridade e atualizar os numeros imediatamente.
  if ((uint32_t)(now - lastUserActivity) < 180) {
    return;
  }

  // Animacao um pouco mais leve para nao disputar SPI com os controles.
  if ((uint32_t)(now - lastNoiseGraphUpdate) >= 220) {
    lastNoiseGraphUpdate = now;
    drawWaveformPreview(mode);
  }
}


// =====================================================
// INICIALIZAR BOTOES
// =====================================================

void setupButtons() {

  uint32_t now =
      millis();


  for (uint8_t i = 0;
       i < BUTTON_COUNT;
       i++) {

    pinMode(
      buttonPins[i],
      INPUT_PULLUP
    );


    bool state =
        digitalRead(
          buttonPins[i]
        );


    buttonStable[i] =
        state;

    buttonLastRead[i] =
        state;

    buttonChangeTime[i] =
        now;

    buttonPressTime[i] =
        now;

    buttonNextRepeat[i] =
        now + 450;
  }
}


// =====================================================
// EVENTO DO BOTAO
//
// Para + e -, permite repeticao automatica.
// =====================================================

uint8_t buttonEvent(
  uint8_t index,
  bool repeatEnabled
) {

  uint32_t now =
      millis();


  bool reading =
      digitalRead(
        buttonPins[index]
      );


  // Mudou eletricamente

  if (
    reading !=
    buttonLastRead[index]
  ) {

    buttonLastRead[index] =
        reading;

    buttonChangeTime[index] =
        now;
  }


  // Debounce 25ms

  if (
    now -
    buttonChangeTime[index]
    >= 25
  ) {

    if (
      reading !=
      buttonStable[index]
    ) {

      buttonStable[index] =
          reading;


      // Pressionado

      if (
        reading ==
        LOW
      ) {

        buttonPressTime[index] =
            now;

        buttonNextRepeat[index] =
            now + 450;

        registerUserActivity();

        return EVENT_PRESS;
      }
    }
  }


  // ==================================================
  // REPETICAO AUTOMATICA
  // ==================================================

  if (
    repeatEnabled &&
    buttonStable[index] == LOW
  ) {

    if (
      (int32_t)(
        now -
        buttonNextRepeat[index]
      )
      >= 0
    ) {

      uint32_t held =
          now -
          buttonPressTime[index];


      uint32_t interval;


      // Primeira velocidade

      if (
        held <
        1500
      ) {

        interval = 120;
      }

      // Acelera

      else if (
        held <
        3000
      ) {

        interval = 65;
      }

      // Muito rapido

      else {

        interval = 35;
      }


      buttonNextRepeat[index] =
          now + interval;

      registerUserActivity();

      return EVENT_REPEAT;
    }
  }


  return EVENT_NONE;
}


// =====================================================
// TEMPO QUE BOTAO ESTA PRESSIONADO
// =====================================================

uint32_t buttonHeldTime(
  uint8_t index
) {

  if (
    buttonStable[index] ==
    HIGH
  ) {

    return 0;
  }


  return
    millis() -
    buttonPressTime[index];
}


// =====================================================
// RANDOM XORSHIFT32
// =====================================================

uint32_t fastRandom32() {

  uint32_t x =
      randomState;


  x ^=
      x << 13;

  x ^=
      x >> 17;

  x ^=
      x << 5;


  randomState = x;


  return x;
}


// =====================================================
// RUIDO BRANCO
// =====================================================

int16_t whiteNoiseSample() {

  uint8_t value =
      fastRandom32() >>
      24;


  return
    (int16_t)value -
    128;
}


// =====================================================
// RESET RUIDO ROSA
// =====================================================

void resetPinkNoise() {

  pinkSum = 0;

  pinkCounter = 0;


  for (uint8_t i = 0;
       i < PINK_ROWS;
       i++) {

    pinkRows[i] =
        whiteNoiseSample();

    pinkSum +=
        pinkRows[i];
  }
}


// =====================================================
// RUIDO ROSA
//
// Voss-McCartney simplificado
// =====================================================

int16_t pinkNoiseSample() {

  pinkCounter++;


  uint32_t counter =
      pinkCounter;


  uint8_t row = 0;


  while (
    ((counter & 1) == 0)
    &&
    row < PINK_ROWS
  ) {

    row++;

    counter >>= 1;
  }


  if (
    row <
    PINK_ROWS
  ) {

    int16_t newValue =
        whiteNoiseSample();


    pinkSum -=
        pinkRows[row];


    pinkRows[row] =
        newValue;


    pinkSum +=
        newValue;
  }


  int32_t total =
      pinkSum +
      whiteNoiseSample();


  // Normalizacao

  total /= 4;


  if (total > 127) {
    total = 127;
  }

  if (total < -127) {
    total = -127;
  }


  return
    (int16_t)total;
}


// =====================================================
// CRIAR TABELA SENO
// =====================================================

void createSineTable() {

  for (int i = 0;
       i < 256;
       i++) {

    float angle =
        2.0f *
        PI *
        (
          (float)i /
          256.0f
        );


    sineTable[i] =
        (int16_t)(
          sinf(angle) *
          127.0f
        );
  }
}


// =====================================================
// FORMAS DE ONDA
// =====================================================

int16_t generateToneSample(
  uint8_t mode,
  uint8_t phase
) {

  // SENO

  if (
    mode ==
    MODE_SINE
  ) {

    return
      sineTable[phase];
  }


  // TRIANGULAR

  if (
    mode ==
    MODE_TRIANGLE
  ) {

    if (
      phase <
      128
    ) {

      return
        -127 +
        (
          ((int32_t)phase * 254)
          /
          127
        );

    } else {

      return
        127 -
        (
          (
            (int32_t)(
              phase - 128
            )
            *
            254
          )
          /
          127
        );
    }
  }


  // QUADRADA

  if (
    mode ==
    MODE_SQUARE
  ) {

    return
      phase < 128 ?
      127 :
      -127;
  }


  // SERRA

  if (
    mode ==
    MODE_SAW
  ) {

    return
      (int16_t)phase -
      128;
  }


  return 0;
}


// =====================================================
// RESET ESTADO DO AUDIO
// =====================================================

void resetAudioState() {

  phaseAccumulator =
      0;


  noiseFilterState =
      0.0f;


  resetPinkNoise();
}


// =====================================================
// CRIAR BUFFER DE AUDIO
// =====================================================

void generateAudioBuffer() {

  // Captura os controles no inicio do buffer

  uint8_t mode =
      currentMode;

  uint32_t frequency =
      controlHz;

  uint8_t amp =
      amplitudeTable[
        amplitudeIndex
      ];

  bool output =
      outputEnabled;


  // ==================================================
  // DDS
  // ==================================================

  uint32_t phaseIncrement =
      (uint32_t)(
        (
          (uint64_t)frequency
          << 32
        )
        /
        SAMPLE_RATE
      );


  // ==================================================
  // FILTRO PASSA-BAIXA PARA RUIDO
  // ==================================================

  float alpha =
      1.0f -
      expf(
        -2.0f *
        PI *
        (
          (float)frequency /
          (float)SAMPLE_RATE
        )
      );


  for (int i = 0;
       i < AUDIO_BUFFER_SIZE;
       i++) {

    int16_t sample = 0;


    // =================================================
    // OUTPUT OFF
    // =================================================

    if (!output) {

      sample = 0;
    }


    // =================================================
    // TONS
    // =================================================

    else if (
      mode <=
      MODE_SAW
    ) {

      uint8_t phase =
          phaseAccumulator >>
          24;


      sample =
          generateToneSample(
            mode,
            phase
          );


      phaseAccumulator +=
          phaseIncrement;
    }


    // =================================================
    // RUIDO BRANCO
    // =================================================

    else if (
      mode ==
      MODE_WHITE
    ) {

      float raw =
          whiteNoiseSample();


      noiseFilterState +=
          alpha *
          (
            raw -
            noiseFilterState
          );


      sample =
          (int16_t)
          noiseFilterState;
    }


    // =================================================
    // RUIDO ROSA
    // =================================================

    else if (
      mode ==
      MODE_PINK
    ) {

      float raw =
          pinkNoiseSample();


      noiseFilterState +=
          alpha *
          (
            raw -
            noiseFilterState
          );


      sample =
          (int16_t)
          noiseFilterState;
    }


    // =================================================
    // AMPLITUDE
    // =================================================

    int32_t scaled =
        (
          (int32_t)sample *
          amp
        )
        /
        100;


    // DAC e 0...255.
    // 128 = ponto central.

    int32_t dacValue =
        128 +
        scaled;


    if (
      dacValue <
      0
    ) {

      dacValue = 0;
    }


    if (
      dacValue >
      255
    ) {

      dacValue = 255;
    }


    audioBuffer[i] =
        (uint8_t)
        dacValue;
  }
}


// =====================================================
// INICIAR DAC
// =====================================================

bool setupDAC() {

  dac_continuous_config_t config = {};


  // DAC canal 0 = GPIO25

  config.chan_mask =
      DAC_CHANNEL_MASK_CH0;


  // DMA

  config.desc_num =
      8;

  config.buf_size =
      1024;


  // 192 kSamples/s

  config.freq_hz =
      SAMPLE_RATE;


  config.offset =
      0;


  config.clk_src =
      DAC_DIGI_CLK_SRC_DEFAULT;


  config.chan_mode =
      DAC_CHANNEL_MODE_SIMUL;


  esp_err_t error;


  error =
      dac_continuous_new_channels(
        &config,
        &dacHandle
      );


  if (
    error !=
    ESP_OK
  ) {

    Serial.print(
      "Erro criando DAC: "
    );

    Serial.println(
      esp_err_to_name(error)
    );

    return false;
  }


  error =
      dac_continuous_enable(
        dacHandle
      );


  if (
    error !=
    ESP_OK
  ) {

    Serial.print(
      "Erro ativando DAC: "
    );

    Serial.println(
      esp_err_to_name(error)
    );

    return false;
  }


  return true;
}


// =====================================================
// TAREFA DE AUDIO
//
// Mantem o DAC sendo alimentado independentemente
// do LCD e dos botoes.
// =====================================================

void audioTask(void *parameter) {

  while (true) {

    if (
      resetAudioStateRequested
    ) {

      resetAudioStateRequested =
          false;

      resetAudioState();
    }


    generateAudioBuffer();


    size_t bytesLoaded = 0;


    esp_err_t error =
        dac_continuous_write(
          dacHandle,
          audioBuffer,
          AUDIO_BUFFER_SIZE,
          &bytesLoaded,
          -1
        );


    if (
      error !=
      ESP_OK
    ) {

      vTaskDelay(
        pdMS_TO_TICKS(1)
      );
    }
  }
}


// =====================================================
// PASSO BASE DE FREQUENCIA
// =====================================================

uint32_t frequencyBaseStep(
  uint32_t frequency
) {

  if (
    frequency <
    100
  ) {

    return 1;
  }


  if (
    frequency <
    1000
  ) {

    return 5;
  }


  if (
    frequency <
    5000
  ) {

    return 10;
  }


  if (
    frequency <
    10000
  ) {

    return 50;
  }


  return 100;
}


// =====================================================
// PASSO COM ACELERACAO
// =====================================================

uint32_t frequencyStep(
  uint8_t buttonIndex
) {

  uint32_t step =
      frequencyBaseStep(
        controlHz
      );


  uint32_t held =
      buttonHeldTime(
        buttonIndex
      );


  // Depois de 1,5s:
  // 5 vezes mais rapido

  if (
    held >=
    1500
  ) {

    step *= 5;
  }


  // Depois de 3s:
  // 20 vezes o passo normal

  if (
    held >=
    3000
  ) {

    step =
        frequencyBaseStep(
          controlHz
        )
        *
        20;
  }


  return step;
}


// =====================================================
// AUMENTAR FREQUENCIA
// =====================================================

void frequencyUp() {

  uint32_t step =
      frequencyStep(4);


  uint32_t value =
      controlHz;


  if (
    value >=
    FREQ_MAX -
    step
  ) {

    value =
        FREQ_MAX;

  } else {

    value +=
        step;
  }


  controlHz =
      value;
}


// =====================================================
// DIMINUIR FREQUENCIA
// =====================================================

void frequencyDown() {

  uint32_t step =
      frequencyStep(3);


  uint32_t value =
      controlHz;


  if (
    value <=
    FREQ_MIN +
    step
  ) {

    value =
        FREQ_MIN;

  } else {

    value -=
        step;
  }


  controlHz =
      value;
}


// =====================================================
// FORMATAR FREQUENCIA
// =====================================================

void formatFrequency(
  char *buffer,
  size_t size,
  uint32_t hz
) {

  if (
    hz <
    1000
  ) {

    snprintf(
      buffer,
      size,
      "%lu",
      (unsigned long)hz
    );

  }

  else if (
    hz <
    10000
  ) {

    snprintf(
      buffer,
      size,
      "%lu.%02luk",
      (unsigned long)(
        hz / 1000
      ),
      (unsigned long)(
        (hz % 1000) / 10
      )
    );

  }

  else {

    snprintf(
      buffer,
      size,
      "%lu.%luk",
      (unsigned long)(
        hz / 1000
      ),
      (unsigned long)(
        (hz % 1000) / 100
      )
    );
  }
}


// =====================================================
// DISPLAY - LAYOUT DO VIDEO, OTIMIZADO
// =====================================================

void invalidateDisplayCache() {

  uiLastMode = -1;
  uiLastFrequency = 0xFFFFFFFFUL;
  uiLastAmplitude = -1;
  uiLastOutput = -1;
}


// =====================================================
// DESENHO ESTATICO DA TELA
// =====================================================

void drawStaticDisplay() {

  tft.fillScreen(ST77XX_BLACK);
  drawDisplayHeader();

  uiScreenReady = true;
  invalidateDisplayCache();
}


// =====================================================
// CAMPO MODO + LEGENDA
// =====================================================

void updateModeField(uint8_t mode) {

  // Limpa somente a area usada pelo modo e pela legenda.
  tft.fillRect(0, 62, tft.width(), 42, ST77XX_BLACK);

  drawCenteredText(
    modeNames[mode],
    67,
    2,
    ST77XX_YELLOW
  );

  if (mode <= MODE_SAW) {
    drawCenteredText("FREQUENCIA", 92, 1, ST77XX_WHITE);
  } else {
    drawCenteredText("CORTE LP", 92, 1, ST77XX_WHITE);
  }
}


// =====================================================
// CAMPO FREQUENCIA
// =====================================================

void updateFrequencyField(uint32_t frequency) {

  char freqText[20];

  if (frequency < 1000) {
    snprintf(
      freqText,
      sizeof(freqText),
      "%lu Hz",
      (unsigned long)frequency
    );
  } else {
    snprintf(
      freqText,
      sizeof(freqText),
      "%lu.%02lu kHz",
      (unsigned long)(frequency / 1000),
      (unsigned long)((frequency % 1000) / 10)
    );
  }

  // Antes o programa apagava os 170x320 pixels inteiros.
  // Agora limpa apenas a faixa do numero da frequencia.
  tft.fillRect(0, 103, tft.width(), 25, ST77XX_BLACK);
  drawCenteredText(freqText, 106, 2, ST77XX_CYAN);
}


// =====================================================
// CAMPO AMPLITUDE
// =====================================================

void updateAmplitudeField(uint8_t amp) {

  char ampText[16];

  snprintf(
    ampText,
    sizeof(ampText),
    "AMP %u%%",
    amp
  );

  // -------------------------------------------------
  // AREA DO AMP
  // -------------------------------------------------
  // Limpa somente o campo da amplitude. A tela inteira
  // nao e redesenhada, mantendo a resposta bem rapida.
  tft.fillRect(0, 129, tft.width(), 34, ST77XX_BLACK);

  drawCenteredText(ampText, 131, 2, ST77XX_WHITE);

  // -------------------------------------------------
  // BARRA GRAFICA DE AMPLITUDE 0...100%
  // -------------------------------------------------
  const int16_t barX = 18;
  const int16_t barY = 150;
  const int16_t barW = 134;
  const int16_t barH = 10;
  const int16_t innerW = barW - 4;

  // Moldura da barra.
  tft.drawRect(barX, barY, barW, barH, ST77XX_WHITE);

  // Fundo da parte interna.
  tft.fillRect(
    barX + 2,
    barY + 2,
    innerW,
    barH - 4,
    ST77XX_BLUE
  );

  // Protecao para nunca ultrapassar 100%.
  uint8_t level = amp;
  if (level > 100) {
    level = 100;
  }

  // Calcula quantos pixels devem ficar preenchidos.
  int16_t filled = (int16_t)(
    ((uint32_t)innerW * (uint32_t)level) / 100UL
  );

  // Barra que cresce/diminui junto com a amplitude.
  if (filled > 0) {
    tft.fillRect(
      barX + 2,
      barY + 2,
      filled,
      barH - 4,
      ST77XX_CYAN
    );
  }
}


// =====================================================
// CAMPO OUTPUT
// =====================================================

void updateOutputField(bool enabled) {

  // O indicador foi deslocado alguns pixels para baixo para
  // abrir espaco para a nova barra grafica de amplitude.
  tft.fillRect(0, 164, tft.width(), 43, ST77XX_BLACK);

  tft.drawLine(18, 166, 151, 166, ST77XX_WHITE);
  tft.drawLine(18, 172, 151, 172, ST77XX_WHITE);
  tft.drawLine(
    22,
    169,
    147,
    169,
    enabled ? ST77XX_GREEN : ST77XX_RED
  );

  if (enabled) {
    drawCenteredText("OUTPUT ON", 181, 2, ST77XX_GREEN);
  } else {
    drawCenteredText("OUTPUT OFF", 181, 2, ST77XX_RED);
  }
}


// =====================================================
// ATUALIZACAO INTELIGENTE DO DISPLAY
// =====================================================

void updateDisplay(bool forceFull = false) {

  if (!lcdAvailable) {
    return;
  }

  uint8_t mode = currentMode;
  uint32_t frequency = controlHz;
  uint8_t amp = amplitudeTable[amplitudeIndex];
  bool output = outputEnabled;

  if (forceFull || !uiScreenReady) {
    drawStaticDisplay();
  }

  bool modeChanged = (uiLastMode != (int16_t)mode);

  if (modeChanged) {
    updateModeField(mode);
    uiLastMode = mode;
  }

  if (uiLastFrequency != frequency) {
    updateFrequencyField(frequency);
    uiLastFrequency = frequency;
  }

  if (uiLastAmplitude != (int16_t)amp) {
    updateAmplitudeField(amp);
    uiLastAmplitude = amp;
  }

  if (uiLastOutput != (int16_t)output) {
    updateOutputField(output);
    uiLastOutput = output;
  }

  // O grafico so precisa ser redesenhado quando muda o modo.
  // A frequencia e a amplitude nao mudam o formato da miniatura.
  if (modeChanged || forceFull) {
    drawWaveformPreview(mode);
    lastNoiseGraphUpdate = millis();
  }
}


// =====================================================
// SERIAL
// =====================================================

void printStatus() {

  uint8_t mode =
      currentMode;


  Serial.println();

  Serial.print(
    "Modo: "
  );

  Serial.println(
    modeNames[mode]
  );


  if (
    mode <=
    MODE_SAW
  ) {

    Serial.print(
      "Frequencia: "
    );

  } else {

    Serial.print(
      "Corte LP: "
    );
  }


  Serial.print(
    controlHz
  );

  Serial.println(
    " Hz"
  );


  Serial.print(
    "Amplitude: "
  );

  Serial.print(
    amplitudeTable[
      amplitudeIndex
    ]
  );

  Serial.println(
    "%"
  );


  Serial.print(
    "Output: "
  );

  Serial.println(
    outputEnabled ?
    "ON" :
    "OFF"
  );
}


// =====================================================
// CONTROLES
// =====================================================

void readControls() {

  bool displayChanged =
      false;


  // ==================================================
  // MODO
  // ==================================================

  if (
    buttonEvent(
      0,
      false
    )
    ==
    EVENT_PRESS
  ) {

    currentMode++;


    if (
      currentMode >=
      MODE_TOTAL
    ) {

      currentMode =
          MODE_SINE;
    }


    resetAudioStateRequested =
        true;


    displayChanged =
        true;


    printStatus();
  }


  // ==================================================
  // AMPLITUDE
  // ==================================================

  if (
    buttonEvent(
      1,
      false
    )
    ==
    EVENT_PRESS
  ) {

    amplitudeIndex++;


    if (
      amplitudeIndex >=
      AMP_COUNT
    ) {

      amplitudeIndex = 0;
    }


    displayChanged =
        true;


    printStatus();
  }


  // ==================================================
  // OUTPUT
  // ==================================================

  if (
    buttonEvent(
      2,
      false
    )
    ==
    EVENT_PRESS
  ) {

    outputEnabled =
        !outputEnabled;


    displayChanged =
        true;


    printStatus();
  }


  // ==================================================
  // FREQUENCIA -
  // ==================================================

  uint8_t downEvent =
      buttonEvent(
        3,
        true
      );


  if (
    downEvent ==
    EVENT_PRESS ||
    downEvent ==
    EVENT_REPEAT
  ) {

    frequencyDown();


    displayChanged =
        true;
  }


  // ==================================================
  // FREQUENCIA +
  // ==================================================

  uint8_t upEvent =
      buttonEvent(
        4,
        true
      );


  if (
    upEvent ==
    EVENT_PRESS ||
    upEvent ==
    EVENT_REPEAT
  ) {

    frequencyUp();


    displayChanged =
        true;
  }


  // ==================================================
  // ATUALIZA LCD APENAS QUANDO NECESSARIO
  // ==================================================

  if (
    displayChanged
  ) {

    updateDisplay();
  }
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  // Backlight do TFT: ligado ao iniciar.
  pinMode(TFT_BL, OUTPUT);
  setBacklight(true);
  lastUserActivity = millis();

  Serial.begin(
    115200
  );


  delay(300);


  Serial.println();
  Serial.println(
    "============================"
  );

  Serial.println(
    " GERADOR DE AUDIO ESP32"
  );

  Serial.println(
    "============================"
  );



  // ==================================================
  // TFT ST7789
  // ==================================================

  lcdInit();


  if (
    lcdAvailable
  ) {

    lcdClear();
    drawDisplayHeader();
    drawCenteredText("ESP32", 112, 2, ST77XX_CYAN);
    drawCenteredText("GERADOR DE AUDIO", 140, 1, ST77XX_WHITE);

    delay(900);
  }


  // ==================================================
  // BOTOES
  // ==================================================

  setupButtons();


  // ==================================================
  // RANDOM
  // ==================================================

  randomState ^=
      micros();

  uiNoiseState ^= micros();


  if (
    randomState ==
    0
  ) {

    randomState =
        0x12345678;
  }


  // ==================================================
  // SENO
  // ==================================================

  createSineTable();


  // ==================================================
  // RUIDO
  // ==================================================

  resetAudioState();


  // ==================================================
  // DAC
  // ==================================================

  if (
    !setupDAC()
  ) {

    Serial.println(
      "ERRO: DAC nao iniciou."
    );


    if (
      lcdAvailable
    ) {

      lcdClear();

      lcdPrintLine(
        0,
        "ERRO DAC"
      );

      lcdPrintLine(
        1,
        "Veja Serial"
      );
    }


    while (true) {
      delay(1000);
    }
  }


  Serial.println(
    "DAC OK - 192 kHz"
  );


  // ==================================================
  // TAREFA DE AUDIO
  // ==================================================

  BaseType_t result =
      xTaskCreatePinnedToCore(
        audioTask,
        "AudioGenerator",
        4096,
        NULL,
        3,
        NULL,
        0
      );


  if (
    result !=
    pdPASS
  ) {

    Serial.println(
      "Erro criando tarefa de audio."
    );


    if (
      lcdAvailable
    ) {

      lcdClear();

      lcdPrintLine(
        0,
        "ERRO TASK"
      );
    }


    while (true) {
      delay(1000);
    }
  }


  // ==================================================
  // DISPLAY
  // ==================================================

  if (
    lcdAvailable
  ) {

    updateDisplay(true);
  }


  printStatus();

  // Comeca a contar os 30 segundos apos terminar o setup.
  lastUserActivity = millis();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  readControls();

  updateNoiseGraphAnimation();

  updateBacklightTimeout();

  delay(1);
}