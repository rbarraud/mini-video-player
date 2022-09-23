/* Open Source Mini Video Player Code (Open Source Mini Video Player - Super Make Something Episode 24)
 * by: Alex - Super Make Something
 * date: September 23rd, 2022
 * license: Creative Commons - Attribution - Non-Commercial.  More information available at: http://creativecommons.org/licenses/by-nc/3.0/
 * links: Project Episode Link: https://youtu.be/67RFm2RMjC4  Livestream Build Episode Link: https://youtu.be/Ob_l5PkH2qA
 * note: Special thank you to GitHub user bepaald for his extensive contributions to bug fixes and code clean-up. Please see the README for more details.
 */

/*
 * require libraries:
 * https://github.com/moononournation/Arduino_GFX.git
 * https://github.com/earlephilhower/ESP8266Audio.git
 * https://github.com/bitbank2/JPEGDEC.git
 */

#define FPS 24
#define MJPEG_BUFFER_SIZE (160 * 128 * 2 / 4)

#include <WiFi.h>
#include <SD.h>
#include <driver/i2s.h>

/* IO Pin Definitions */
int SD_MISO = 12;
int SD_SCK = 14;
int SD_MOSI = 13;
int SD_CS = 4;
int LCD_SCK = 18;
int LCD_MISO = 19;
int LCD_MOSI = 23;
int LCD_DC_A0 = 27;
int LCD_RESET = 33;
int LCD_CS = 5;

int BTN_NEXT = 21;
int BTN_PREV = 15;

/* Arduino_GFX */
#include <Arduino_GFX_Library.h>
Arduino_ESP32SPI *bus = new Arduino_ESP32SPI(LCD_DC_A0 /* DC */, LCD_CS /* CS */, LCD_SCK /* SCK */, LCD_MOSI /* MOSI */, LCD_MISO /* MISO */);
/* NOTE - IF RED AND BLUE COLORS ARE SWAPPED, FLIP bool bgr in below constructor. */
Arduino_GFX *gfx = new Arduino_ST7735(bus, LCD_RESET /* RST */, 3 /* rotation */, false /* IPS */, ST7735_TFTWIDTH /* w*/, ST7735_TFTHEIGHT /* h*/,
                                      0 /* col_offset1 */, 0 /* row_offset1 */, 0 /* col_offset2 */, 0 /* row_offset2 */, false /* bgr */);

/* MP3 Audio */
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
static AudioGeneratorMP3 *mp3 = NULL;
static AudioFileSourceFS *aFile = NULL;
static AudioOutputI2S *out = NULL;

/* MJPEG Video */
#include "MjpegClass.h"
static MjpegClass mjpeg;

static unsigned long total_show_video = 0;

int noFiles = 0; // Number of media files on SD Card in root directory
String videoFilename;
String audioFilename;

int fileNo = 1; // Variable for which (video, audio) filepair to play first
bool buttonPressed = false; // file change button pressed variable
bool fullPlaythrough = true;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

File root;

void IRAM_ATTR incrFileNo()
{
  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    fileNo = fileNo+1;
    if (fileNo>noFiles/2) // Loop around
    {
      fileNo = 1;
    }
    buttonPressed = true;
    lastDebounceTime = millis();
    fullPlaythrough = false;
    Serial.print("Button Pressed! Current Debounce Time: ");
    Serial.println(lastDebounceTime);
  }
}

void IRAM_ATTR decrFileNo()
{
  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    fileNo = fileNo-1;
    if (fileNo<1) // Loop around
    {
      fileNo = noFiles/2;
    }
    buttonPressed = true;
    lastDebounceTime = millis();
    fullPlaythrough = false;
    Serial.print("Button Pressed! Current Debounce Time: ");
    Serial.println(lastDebounceTime);
  }
}


void setup()
{
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);

  attachInterrupt(BTN_NEXT, incrFileNo, RISING);
  attachInterrupt(BTN_PREV, decrFileNo, RISING);

  // Init audio
  out = new AudioOutputI2S(0,1,128);
  mp3 = new AudioGeneratorMP3();
  aFile = new AudioFileSourceFS(SD);

  // Init Video
  gfx->begin();
  gfx->fillScreen(BLACK);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS,SPI,40000000))
  {
    Serial.println(F("ERROR: SD card mount failed!"));
    gfx->println(F("ERROR: SD card mount failed!"));
  }
  else
  {
    root = SD.open("/");
    noFiles = getNoFiles(root);
    Serial.print("Found ");
    Serial.print(noFiles);
    Serial.println(" in root directory!");
    Serial.println("Starting playback!");
  }

}

int getNoFiles(File dir)
{
  while (true)
  {
    File entry =  dir.openNextFile();
    if (! entry)
    {
      // no more files
      break;
    }

    if (entry.isDirectory())
    {
      // Skip file if in subfolder
       entry.close(); // Close folder entry
    }
    else
    {
      Serial.println(entry.name());
      entry.close();
      ++noFiles;
    }
  }
  return noFiles;
}

void getFilenames(File dir, int fileNo)
{
  int fileCounter = 1;
  while (true)
  {
    File entry =  dir.openNextFile();
    if (! entry)
    {
      // no more files
      break;
    }

    Serial.println(entry.name());

    if (entry.isDirectory())
    {
      // Skip file if in subfolder
       entry.close(); // Close folder entry
    }
    else // Get filename
    {
      while (fileCounter<fileNo) // While not at correct file pair number
      {
        entry.close(); //Close current video file
        entry =  dir.openNextFile(); //Open and close corresponding audio file
        entry.close();
        fileCounter = fileCounter+1;
        entry = dir.openNextFile(); // Open next file for reading
      }
  
  // arduino-esp32 < 2.0.0 were based on IDF 3.3.5 (and earlier).
  //               >= 2.0.0 on IDF 4.4 (and later) -> this version includes File::path()
  #if ESP_IDF_VERSION_MAJOR > 4 || (ESP_IDF_VERSION_MAJOR == 4 && ESP_IDF_VERSION_MINOR >= 4)
      videoFilename = entry.path();
  #else
      videoFilename = entry.name();
  #endif
      Serial.print("Loading video: ");
      Serial.println(videoFilename);
      entry.close();
      entry =  dir.openNextFile();
  #if ESP_IDF_VERSION_MAJOR > 4 || (ESP_IDF_VERSION_MAJOR == 4 && ESP_IDF_VERSION_MINOR >= 4)
      audioFilename = entry.path();
  #else
      audioFilename = entry.name();
  #endif
      Serial.print("Loading audio: ");
      Serial.println(audioFilename);
      entry.close();
      break;
    }
  }
}

void playVideo(String videoFilename, String audioFilename)
{
  int next_frame = 0;
  int skipped_frames = 0;
  unsigned long total_play_audio = 0;
  unsigned long total_read_video = 0;
  unsigned long total_decode_video = 0;
  unsigned long start_ms, curr_ms, next_frame_ms;

  int brightPWM = 0;

  Serial.println("In playVideo() loop!");

  if (mp3 && mp3->isRunning())
    mp3->stop();
  if (!aFile->open(audioFilename.c_str()))
    Serial.println(F("Failed to open audio file"));   
  Serial.println("Created aFile!");

  File vFile = SD.open(videoFilename);
  Serial.println("Created vFile!");

  uint8_t *mjpeg_buf = (uint8_t *)malloc(MJPEG_BUFFER_SIZE);
  if (!mjpeg_buf)
  {
    Serial.println(F("mjpeg_buf malloc failed!"));
  }
  else
  {
    // init Video
    mjpeg.setup(&vFile, mjpeg_buf, drawMCU, false, true); //MJPEG SETUP -> bool setup(Stream *input, uint8_t *mjpeg_buf, JPEG_DRAW_CALLBACK *pfnDraw, bool enableMultiTask, bool useBigEndian)
  }

  if (!vFile || vFile.isDirectory())
  {
    Serial.println(("ERROR: Failed to open "+ videoFilename +".mjpeg file for reading"));
    gfx->println(("ERROR: Failed to open "+ videoFilename +".mjpeg file for reading"));
  }
  else
  {
    // init audio
    if (!mp3->begin(aFile, out))
      Serial.println(F("Failed to start audio!"));
    start_ms = millis();
    curr_ms = start_ms;
    next_frame_ms = start_ms + (++next_frame * 1000 / FPS);

    while (vFile.available() && buttonPressed == false)
    {
      // Read video
      mjpeg.readMjpegBuf();
      total_read_video += millis() - curr_ms;
      curr_ms = millis();
      if (millis() < next_frame_ms) // check show frame or skip frame
      {
        // Play video
        mjpeg.drawJpg();
        total_decode_video += millis() - curr_ms;
      }
      else
      {
        ++skipped_frames;
        Serial.println(F("Skip frame"));
      }
      curr_ms = millis();
      // Play audio
      if (mp3->isRunning() && !mp3->loop())
      {
        mp3->stop();
      }

      total_play_audio += millis() - curr_ms;
      while (millis() < next_frame_ms)
      {
        vTaskDelay(1);
      }
      curr_ms = millis();
      next_frame_ms = start_ms + (++next_frame * 1000 / FPS);
    }
    if (fullPlaythrough == false)
    {
      mp3->stop();
    }
    buttonPressed = false; // reset buttonPressed boolean
    int time_used = millis() - start_ms;
    int total_frames = next_frame - 1;
    Serial.println(F("MP3 audio MJPEG video end"));
    vFile.close();
    aFile -> close();
  }
  if (mjpeg_buf)
  {
    free(mjpeg_buf);
  }
}

// pixel drawing callback
static int drawMCU(JPEGDRAW *pDraw)
{
  unsigned long s = millis();
  gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  total_show_video += millis() - s;
  return 1;
}

void loop()
{
  root = SD.open("/");
  Serial.print("fileNo: ");
  Serial.println(fileNo);
  getFilenames(root, fileNo);
  playVideo(videoFilename,audioFilename);

  if (fullPlaythrough == true) // Check fullPlaythrough boolean to avoid double increment of fileNo
  {
    fileNo = fileNo+1; // Increment fileNo to play next video
    if (fileNo>noFiles/2) // If exceeded number of files, reset counter
    {
      fileNo = 1;
    }
  }
  else // Reset fullPlaythrough boolean
  {
    fullPlaythrough = true;
  }
}
