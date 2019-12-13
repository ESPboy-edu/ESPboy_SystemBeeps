//by Shiru 12.12.2019
//shiru@mail.ru
//https://www.patreon.com/shiru8bit

#include <Adafruit_MCP23017.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_GFX.h>
#include <ESP8266WiFi.h>

#include "glcdfont.c"

#include "gfx/espboy.h"
#include "gfx/title.h"

#include "mus/aon.h"
#include "mus/asf.h"
#include "mus/bad.h"
#include "mus/btl.h"
#include "mus/clo.h"
#include "mus/coy.h"
#include "mus/dld.h"
#include "mus/fin.h"
#include "mus/flo.h"
#include "mus/hsh.h"
#include "mus/hst.h"
#include "mus/led.h"
#include "mus/mnc.h"
#include "mus/mym.h"
#include "mus/pxl.h"
#include "mus/run.h"
#include "mus/sqw.h"
#include "mus/srv.h"
#include "mus/ssd.h"
#include "mus/stf.h"
#include "mus/sys.h"
#include "mus/tmb.h"
#include "mus/txr.h"

#define MCP23017address 0 // actually it's 0x20 but in <Adafruit_MCP23017.h> lib there is (x|0x20) :)

//PINS
#define LEDPIN         D4
#define SOUNDPIN       D3

//SPI for LCD
#define csTFTMCP23017pin 8
#define TFT_RST       -1
#define TFT_DC        D8
#define TFT_CS        -1

Adafruit_MCP23017 mcp;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

volatile int sound_out;
volatile int sound_cnt;
volatile int sound_load;
volatile int sound_duration;
volatile int frame_cnt;

volatile const uint8_t* music_data;
volatile int music_ptr;
volatile int music_wait;
volatile int music_period;

int pad_state;
int pad_state_prev;
int pad_state_t;

#define PIT_CLOCK       1193180

#define SAMPLE_RATE     96000
#define FRAME_RATE      120

#define PAD_LEFT        0x01
#define PAD_UP          0x02
#define PAD_DOWN        0x04
#define PAD_RIGHT       0x08
#define PAD_A           0x10
#define PAD_B           0x20
#define PAD_ANY         (PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT|PAD_A|PAD_B)

#define PLAYLIST_HEIGHT 15
#define PLAYLIST_LEN    26

const uint8_t* const playlist[PLAYLIST_LEN * 2] = {
  (const uint8_t*)"SIDE A:>           ", 0,
  (const uint8_t*)" System Beeps      ", m_sys,
  (const uint8_t*)" Too Many Bits     ", m_tmb,
  (const uint8_t*)" Battery Low       ", m_btl,
  (const uint8_t*)" Monoculear        ", m_mnc,
  (const uint8_t*)" Head Step         ", m_hst,
  (const uint8_t*)" Bad Sector        ", m_bad,
  (const uint8_t*)" Dial-Down         ", m_dld,
  (const uint8_t*)" Handshake         ", m_hsh,
  (const uint8_t*)" Floppy Flips      ", m_flo,
  (const uint8_t*)" Pixel Rain        ", m_pxl,
  (const uint8_t*)" Single-Sided Drive", m_ssd,
  (const uint8_t*)" Twinkle LED       ", m_led,
  (const uint8_t*)" Clocking Ticks    ", m_clo,
  (const uint8_t*)" TX/RX             ", m_txr,
  (const uint8_t*)" Serverside        ", m_srv,
  (const uint8_t*)" Staff Roll        ", m_stf,
  (const uint8_t*)"SIDE B:>           ", 0,
  (const uint8_t*)" Astro Force       ", m_asf,
  (const uint8_t*)" Run Under Fire    ", m_run,
  (const uint8_t*)" My Mission        ", m_mym,
  (const uint8_t*)" Square Wave       ", m_sqw,
  (const uint8_t*)" Final Stretch     ", m_fin,
  (const uint8_t*)" Coming Year       ", m_coy,
  (const uint8_t*)"SIDE X:>           ", 0,
  (const uint8_t*)" AONDEMO Soundtrack", m_aon
};

int playlist_cur;

#define SPEC_BANDS        21
#define SPEC_HEIGHT       32
#define SPEC_RANGE        650
#define SPEC_SX           1
#define SPEC_SY           56
#define SPEC_BAND_WIDTH   6
#define SPEC_DECAY        1

volatile int spec_levels[SPEC_BANDS];



int check_key()
{
  pad_state_prev = pad_state;
  pad_state = 0;

  for (int i = 0; i < 8; i++)
  {
    if (!mcp.digitalRead(i)) pad_state |= (1 << i);
  }

  pad_state_t = pad_state ^ pad_state_prev & pad_state;

  return pad_state;
}



//0 no timeout, otherwise timeout in ms

void wait_any_key(int timeout)
{
  timeout /= 100;

  while (1)
  {
    check_key();

    if (pad_state_t&PAD_ANY) break;

    if (timeout)
    {
      --timeout;

      if (timeout <= 0) break;
    }

    delay(100);
  }
}



void spec_add()
{
  int i, off;
  const int curve[5] = {SPEC_HEIGHT / 8, SPEC_HEIGHT / 5, SPEC_HEIGHT / 2, SPEC_HEIGHT / 5, SPEC_HEIGHT / 8};

  if (music_period)
  {
    off = (PIT_CLOCK / music_period) / (SPEC_RANGE / SPEC_BANDS) - 2;

    if (off > SPEC_BANDS - 1) off = SPEC_BANDS - 1;

    for (i = 0; i < 5; ++i)
    {
      if (off >= 0 && off < SPEC_BANDS)
      {
        spec_levels[off] += curve[i];

        if (spec_levels[off] > SPEC_HEIGHT) spec_levels[off] = SPEC_HEIGHT;
      }

      ++off;
    }
  }
}

void spec_update()
{
  int i;

  for (i = 0; i < SPEC_BANDS; ++i)
  {
    spec_levels[i] -= SPEC_DECAY;
    if (spec_levels[i] < 0) spec_levels[i] = 0;
  }
}



void set_speaker(int period, int duration)
{
  int div;

  sound_duration = SAMPLE_RATE / 1000 * duration;

  music_period = period;

  if (!period)
  {
    sound_out |= 2;
  }
  else
  {
    sound_out &= ~2;

    div = PIT_CLOCK * 2 / period;

    if (!div) div = 1;

    sound_load = SAMPLE_RATE / div;
  }
}



void music_start(const uint8_t* data)
{
  music_ptr = 1;
  music_wait = 0;
  music_data = data;
}



void music_stop()
{
  music_data = NULL;
  set_speaker(0, 0);
}



void music_update()
{
  int wait, period;

  spec_update();
  spec_add();

  if (!music_data) return;

  if (music_wait)
  {
    --music_wait;
    if (music_wait) return;
  }

  wait = pgm_read_byte((const void*)&music_data[music_ptr++]);

  if (!wait)
  {
    music_stop();
  }
  else if (wait < 255)
  {
    music_wait = wait;

    period   = pgm_read_byte((const void*)&music_data[music_ptr++]);
    period  |= (pgm_read_byte((const void*)&music_data[music_ptr++]) << 8);

    set_speaker(period, 0);
  }
}



void ICACHE_RAM_ATTR sound_ISR()
{
  if (sound_out)
  {
    GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, _BV(SOUNDPIN));   //clear
  }
  else
  {
    GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, _BV(SOUNDPIN));   //set
  }

  --sound_cnt;

  if (sound_cnt < 0)
  {
    sound_cnt = sound_load;
    sound_out ^= 1;
  }

  --frame_cnt;

  if (frame_cnt < 0)
  {
    frame_cnt += SAMPLE_RATE / FRAME_RATE;

    music_update();
  }

  if (sound_duration > 0)
  {
    --sound_duration;

    if (!sound_duration) sound_out |= 2;
  }
}



//render part of a 8-bit uncompressed BMP file
//no clipping
//uses line buffer to draw it much faster than through writePixel

void drawBMP8Part(int16_t x, int16_t y, const uint8_t bitmap[], int16_t dx, int16_t dy, int16_t w, int16_t h)
{
  int32_t i, j, bw, bh, wa, off, col, rgb;
  static uint16_t buf[128];

  bw = pgm_read_dword(&bitmap[0x12]);
  bh = pgm_read_dword(&bitmap[0x16]);
  wa = (bw + 3) & ~3;

  if (w >= h)
  {
    for (i = 0; i < h; ++i)
    {
      off = 54 + 256 * 4 + (bh - 1 - (i + dy)) * wa + dx;

      for (j = 0; j < w; ++j)
      {
        col = pgm_read_byte(&bitmap[off++]);
        rgb = pgm_read_dword(&bitmap[54 + col * 4]);
        buf[j] = ((rgb & 0xf8) >> 3) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf80000) >> 8);
      }

      tft.drawRGBBitmap(x, y + i, buf, w, 1);
    }
  }
  else
  {
    for (i = 0; i < w; ++i)
    {
      off = 54 + 256 * 4 + (bh - 1 - dy) * wa + i + dx;

      for (j = 0; j < h; ++j)
      {
        col = pgm_read_byte(&bitmap[off]);
        rgb = pgm_read_dword(&bitmap[54 + col * 4]);
        buf[j] = ((rgb & 0xf8) >> 3) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf80000) >> 8);
        off -= wa;
      }

      tft.drawRGBBitmap(x + i, y, buf, 1, h);
    }
  }
}



void drawCharFast(int x, int y, int c, int16_t color, int16_t bg)
{
  int i, j, line;
  static uint16_t buf[5 * 8];

  for (i = 0; i < 5; ++i)
  {
    line = pgm_read_byte(&font[c * 5 + i]);

    for (j = 0; j < 8; ++j)
    {
      buf[j * 5 + i] = (line & 1) ? color : bg;
      line >>= 1;
    }
  }

  tft.drawRGBBitmap(x, y, buf, 5, 8);
}



void printFast(int x, int y, char* str, int16_t color)
{
  char c;

  while (1)
  {
    c = *str++;

    if (!c) break;

    drawCharFast(x, y, c, color, 0);
    x += 6;
  }
}



bool espboy_logo_effect(int out)
{
  int i, j, w, h, sx, sy, off, st, anim;

  sx = 32;
  sy = 28;
  w = 64;
  h = 72;
  st = 8;

  for (anim = 0; anim < st; ++anim)
  {
    if (check_key()&PAD_ANY) return false;

    if (!out) set_speaker(200 + anim * 50, 5);

    for (i = 0; i < w / st; ++i)
    {
      for (j = 0; j < st; ++j)
      {
        off = anim - (7 - j);

        if (out) off += 8;

        if (off < 0 || off >= st) off = 0; else off += i * st;

        drawBMP8Part(sx + i * st + j, sy, g_espboy, off, 0, 1, h);
      }
    }

    delay(1000 / 30);
  }

  return true;
}



bool title_screen_effect(int out)
{
  int16_t order[32 * 32];
  int x, y, i, j, pos, temp, off1, off2, wh;

  wh = 8;

  for (i = 0; i < 32 * 32; ++i) order[i] = i;

  for (i = 0; i < 32; i += wh)
  {
    for (j = 0; j < 32 * wh; ++j)
    {
      off1 = (i * 32) + (rand() & (32 * wh - 1));
      off2 = (i * 32) + (rand() & (32 * wh - 1));

      if (off1 >= 32 * 32) continue;
      if (off2 >= 32 * 32) continue;

      temp = order[off1];
      order[off1] = order[off2];
      order[off2] = temp;
    }
  }

  i = 0;

  while (i < 32 * 32)
  {
    if (check_key()&PAD_ANY) return false;

    set_speaker(500 + i + (rand() & 511), !out ? 4 : 1);

    for (j = 0; j < (!out ? 16 : 32); ++j)
    {
      pos = order[i++];
      x = pos % 32 * 4;
      y = pos / 32 * 4;

      if (!out)
      {
        drawBMP8Part(x, y, g_title, x, y, 4, 4);
      }
      else
      {
        tft.fillRect(x, y, 4, 4, ST7735_BLACK);
      }
    }

    delay(1000 / 60);
  }

  return true;
}



void setup()
{
  //serial init

  Serial.begin(115200);

  //disable wifi to safe some battery power

  WiFi.mode(WIFI_OFF);


  //buttons on mcp23017 init

  mcp.begin(MCP23017address);
  delay(100);

  for (int i = 0; i < 8; i++)
  {
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);
  }

  pad_state = 0;
  pad_state_prev = 0;
  pad_state_t = 0;

  //TFT init
  mcp.pinMode(csTFTMCP23017pin, OUTPUT);
  mcp.digitalWrite(csTFTMCP23017pin, LOW);
  tft.initR(INITR_144GREENTAB);
  delay(100);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);


  //sound init

  pinMode(SOUNDPIN, OUTPUT);

  sound_cnt = 0;
  sound_load = 0;
  sound_out = 2;
  frame_cnt = 0;
  sound_duration = 0;
  music_data = NULL;

  noInterrupts();
  timer1_attachInterrupt(sound_ISR);
  timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
  timer1_write(ESP.getCpuFreqMHz() * 1000000 / SAMPLE_RATE);
  interrupts();

  delay(300);
}


void playlist_display(bool cur)
{
  int i, sy, pos;

  pos = playlist_cur - PLAYLIST_HEIGHT / 2;

  if (pos < 0) pos = 0;
  if (pos > PLAYLIST_LEN - PLAYLIST_HEIGHT) pos = PLAYLIST_LEN - PLAYLIST_HEIGHT;

  sy = 4;

  for (i = 0; i < PLAYLIST_HEIGHT; ++i)
  {
    drawCharFast(2, sy, ' ', ST77XX_WHITE, ST77XX_BLACK);

    printFast(4, sy, (char*)playlist[pos * 2], (playlist[pos * 2][0] == ' ') ? ST77XX_WHITE : ST77XX_YELLOW);

    if ((pos == playlist_cur) && cur) drawCharFast(2, sy, 0xdb, ST77XX_WHITE, ST77XX_BLACK);

    ++pos;

    sy += 8;
  }
}



void playlist_move(int dx)
{
  while (1)
  {
    playlist_cur += dx;

    if (playlist_cur < 0) playlist_cur = PLAYLIST_LEN - 1;
    if (playlist_cur >= PLAYLIST_LEN) playlist_cur = 0;

    if (playlist[playlist_cur * 2][0] == ' ') break;
  }
}



void playlist_screen()
{
  bool change;
  int frame;

  set_speaker(0, 0);

  tft.fillScreen(ST77XX_BLACK);

  change = true;
  frame = 0;

  while (1)
  {
    if (change)
    {
      playlist_display((frame & 32) ? true : false);
      change = false;
    }

    check_key();

    if (pad_state_t & PAD_UP)
    {
      playlist_move(-1);
      change = true;
      frame = 32;
    }

    if (pad_state_t & PAD_DOWN)
    {
      playlist_move(1);
      change = true;
      frame = 32;
    }

    if (pad_state_t & (PAD_A | PAD_B)) break;

    delay(1);

    ++frame;

    if (!(frame & 31)) change = true;
  }
}



void playing_screen()
{
  int i, h, sx, sy, off;

  for (i = 0; i < SPEC_BANDS; ++i) spec_levels[i] = 0;

  tft.fillScreen(ST77XX_BLACK);

  printFast(4, 16, "Now playing...", ST77XX_YELLOW);
  printFast(4, 24, (char*)playlist[playlist_cur * 2], ST77XX_WHITE);

  sx = SPEC_SX;

  for (i = 0; i < SPEC_BANDS; ++i)
  {
    tft.fillRect(sx, SPEC_SY + SPEC_HEIGHT + 1, SPEC_BAND_WIDTH - 1, 1, ST77XX_WHITE);
    sx += SPEC_BAND_WIDTH;
  }

  music_start(playlist[playlist_cur * 2 + 1]);

  while (music_data)
  {
    sx = SPEC_SX;
    sy = 56;

    for (i = 0; i < SPEC_BANDS; ++i)
    {
      h = spec_levels[i];

      if (h > SPEC_HEIGHT) h = SPEC_HEIGHT;

      tft.fillRect(sx, sy, 5, SPEC_HEIGHT - h, ST77XX_BLACK);
      tft.fillRect(sx, sy + SPEC_HEIGHT - h, SPEC_BAND_WIDTH - 1, h, ST77XX_GREEN);

      sx += SPEC_BAND_WIDTH;
    }

    check_key();

    if (pad_state_t) break;

    delay(1000 / 120);
  }

  music_stop();
}



void loop()
{
  playlist_cur = 1;

  //logo and title screen (skippable)

  if (espboy_logo_effect(0))
  {
    wait_any_key(1000);
    if (espboy_logo_effect(1))
    {
      if (title_screen_effect(0))
      {
        wait_any_key(3000);
        title_screen_effect(1);
      }
    }
  }

  //main loop

  while (1)
  {
    playlist_screen();
    playing_screen();
  }
}
