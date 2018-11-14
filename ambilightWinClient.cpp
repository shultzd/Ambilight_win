// ambilightServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdlib.h>
#include <windows.h>
#include <chrono> //For time measurements

///////////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////////

#define SEC_TO_MSEC (1000) // 1 Sec = 1000 mSec
#define MSEC_TO_USEC (1000) // 1 mSec = 1000 uSec

#define NUM_VALUES_PER_WIN_PIXEL   (4)

///////////////////////////////////////////////////////////////////////////////////
// Classes
///////////////////////////////////////////////////////////////////////////////////

class serialCon {
private:
   HANDLE hSerial;
   BOOL isSerialConnected;
   wchar_t *portName = L"\\\\.\\COM1";

public:
   serialCon() {
      hSerial = INVALID_HANDLE_VALUE;
      isSerialConnected = FALSE;
   }

   ~serialCon() { 
      this->closeConnection(); 
   }
   
   BOOL isConnected(){ return isSerialConnected; };
   void closeConnection() 
   { 
      if (hSerial != INVALID_HANDLE_VALUE)
      {
         CloseHandle(hSerial);
         hSerial = INVALID_HANDLE_VALUE;
         isSerialConnected = FALSE;
      }
   };

   BOOL setupSerialComm();
   void sendToArduino(BYTE *finalPixels, int numPixels);
};

class leds {
private:
   serialCon serialConnection;
   float brightnessCoef;
   BOOL isReady;
   
public:
   //LEDs layout
   static const unsigned int numHorisontal = 28;
   static const unsigned int numVertical = 16;
   static const unsigned int numValuesPerPixel = 3; //LS2802b parameters
   static const unsigned int totalAmountOfLeds = ((numHorisontal + numVertical) * 2); // Amount of LEDs on all 4 sides
   static const unsigned int totalNumBytesToSend = totalAmountOfLeds * numValuesPerPixel;

   leds() {
      brightnessCoef = 1.0;
      isReady = FALSE;

      tryConnect(TRUE);
   }

   ~leds() {
      clearLeds();
   }

   float getBrightnessCoef() { return brightnessCoef; }
   BOOL isConnected() { return serialConnection.isConnected() && isReady; }
   void setSolidColor(const BYTE red, const BYTE green, const BYTE blue);
   void clearLeds() { setSolidColor(0, 0, 0); }
   void runLedTest();
   void setLeds(BYTE *finalPixels, int numPixels){ if (isReady) { serialConnection.sendToArduino(finalPixels, numPixels); } }
   
   void tryConnect(BOOL runTest) 
   {
      if (!serialConnection.isConnected())
      {
         BOOL res;
         isReady = FALSE;

         res = serialConnection.setupSerialComm();
         if (res)
         {
            if (runTest)
            {
               runLedTest();
            }
            isReady = TRUE;
         }
      }
   }
};

class screenResolution {
public:
   unsigned int width;
   unsigned int height;

   screenResolution()
   {
      // Notify OS that this application is aware of screen scaling and will handle it independently (ignore it).
      // This is needed to get the actual screen resolution, instead of the scaled one.
      SetProcessDPIAware();

      width = GetSystemMetrics(SM_CXSCREEN);
      height = GetSystemMetrics(SM_CYSCREEN);

      printf("Screen resolution detected: %dx%d\n", width, height);
   }

   void update()
   {
      unsigned int newWidth = GetSystemMetrics(SM_CXSCREEN);
      unsigned int newHeight = GetSystemMetrics(SM_CYSCREEN);

      if ((newWidth != this->width) || (newHeight != this->height))
      {
         this->width = newWidth;
         this->height = newHeight;

         printf("Screen resolution detected: %dx%d\n", newWidth, newHeight);
      }
   }
};

class screenEdge {
public:
   unsigned int top, bottom, right, left;

   screenEdge() {
      top = MAXUINT32;
      bottom = MAXUINT32;
      right = MAXUINT32;
      left = MAXUINT32;
   }
};

class screenEdgeDetection {
public:
   const double sensitivity = 0.01;
   const BOOL stabilityEnable = TRUE;

   screenEdge suggestedEdges;
};

class screen {
private:
   screenEdgeDetection edgeDetection;

public:
   screenEdge curEdges;
   screenResolution res;

   screen() {
      setDefaultEdges();
   }

   void setDefaultEdges() {
      curEdges.top = 0;
      curEdges.bottom = res.height - 1;
      curEdges.left = 0;
      curEdges.right = res.width - 1;
   }

   void detectEdges();
};

leds gLeds;
screen gScreen;

///////////////////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////////////////

// Flow control 
BOOL gExitProgram = FALSE;
const unsigned int gEdgeDetectionCheckIntervalSec = 5;
const BOOL edgeDetection_enable = TRUE;

BOOL serialCon::setupSerialComm()
{
   DCB dcbSerialParams = { 0 };
   COMMTIMEOUTS timeouts = { 0 };
   
   isSerialConnected = FALSE;

   // Open the highest available serial port number
   hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   if (hSerial == INVALID_HANDLE_VALUE)
   {
      return FALSE;
   }

   // Set device parameters (115200 baud, 1 start bit,
   // 1 stop bit, no parity)
   dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
   if (GetCommState(hSerial, &dcbSerialParams) == 0)
   {
      CloseHandle(hSerial);
      return FALSE;
   }

   dcbSerialParams.BaudRate = CBR_115200;
   dcbSerialParams.ByteSize = 8;
   dcbSerialParams.StopBits = ONESTOPBIT;
   dcbSerialParams.Parity = NOPARITY;
   if (SetCommState(hSerial, &dcbSerialParams) == 0)
   {
      CloseHandle(hSerial);
      return FALSE;
   }

   // Set COM port timeout settings
   timeouts.ReadIntervalTimeout = 50;
   timeouts.ReadTotalTimeoutConstant = 50;
   timeouts.ReadTotalTimeoutMultiplier = 10;
   timeouts.WriteTotalTimeoutConstant = 50;
   timeouts.WriteTotalTimeoutMultiplier = 10;
   if (SetCommTimeouts(hSerial, &timeouts) == 0)
   {
      CloseHandle(hSerial);
      return FALSE;
   }

   isSerialConnected = TRUE;
   return TRUE;
}

void serialCon::sendToArduino(BYTE *finalPixels, int numPixels)
{
   const BYTE preamble[5] = { 'd', 'a', 'n', 'n', 'y' };
   const BYTE ackValue = 'k';

   const int preambleSize = 5;
   const int ackSize = 1;

   // Send preamble
   DWORD bytes_written;
   if (!WriteFile(hSerial, preamble, preambleSize, &bytes_written, NULL))
   {
      CloseHandle(hSerial);
      isSerialConnected = FALSE;
      return;
   }

   // Send specified text (remaining command line arguments)
   if (!WriteFile(hSerial, finalPixels, numPixels, &bytes_written, NULL))
   {
      CloseHandle(hSerial);
      isSerialConnected = FALSE;
      return;
   }

   //Wait for a response
   BYTE ackByte = 0;
   DWORD bytes_read = 0;
   auto start_time = std::chrono::high_resolution_clock::now();
   do
   {
      if (!ReadFile(hSerial, &ackByte, ackSize, &bytes_read, NULL))
      {
         CloseHandle(hSerial);
         isSerialConnected = FALSE;
         return;
      }

      auto end_time = std::chrono::high_resolution_clock::now();
      auto time = end_time - start_time;
      if ((std::chrono::duration_cast<std::chrono::microseconds>(time).count()) > (0.5 * MSEC_TO_USEC * SEC_TO_MSEC))
      {
         CloseHandle(hSerial);
         isSerialConnected = FALSE;
         return;
      }
   } while (bytes_read == 0);

   if (ackByte != ackValue)
   {
      CloseHandle(hSerial);
      isSerialConnected = FALSE;
      return;
   }
}

void leds::setSolidColor(const BYTE red, const BYTE green, const BYTE blue)
{
   BYTE* finalPixals = new BYTE[totalNumBytesToSend];
   unsigned int i = 0;

   for (i = 0; i < totalNumBytesToSend; i += numValuesPerPixel)
   {
      finalPixals[i + 0] = red;
      finalPixals[i + 1] = green;
      finalPixals[i + 2] = blue;
   }

   serialConnection.sendToArduino(finalPixals, totalNumBytesToSend);

   delete[] finalPixals;
}

void screen::detectEdges()
{
   unsigned int newTopEdge, newBottomEdge, newLeftEdge, newRightEdge;
   unsigned int x, y, x_add, valCount, sum;
   double precession;
   BYTE* lpPixels;

   // copy screen to bitmap
   HDC     hScreen = GetDC(NULL);
   HDC     hDC = CreateCompatibleDC(hScreen);
   HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, res.width, res.height);
   BITMAPINFO MyBMInfo = { 0 };

   SelectObject(hDC, hBitmap);

   if ((curEdges.top == MAXINT32) || (curEdges.bottom == MAXINT32)
      || (curEdges.left == MAXINT32) || (curEdges.right == MAXINT32))
   {
      printf("Initial screen edge detection, setting default values\n");
      setDefaultEdges();
   }

   newTopEdge = curEdges.top;
   newBottomEdge = curEdges.bottom;
   newLeftEdge = curEdges.left;
   newRightEdge = curEdges.right;

   MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

   // Get the BITMAPINFO structure from the bitmap
   if (0 == GetDIBits(hDC, hBitmap, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS))
   {
      printf("Error!!! detectScreenEdges:GetDIBits 1 failed\n");
      return;
   }

   MyBMInfo.bmiHeader.biBitCount = 32;
   MyBMInfo.bmiHeader.biCompression = BI_RGB;  // no compression -> easier to use
   MyBMInfo.bmiHeader.biHeight = abs(MyBMInfo.bmiHeader.biHeight); // correct the bottom-up ordering of lines

   if (!BitBlt(hDC, 0, 0, res.width, res.height, hScreen, 0, 0, SRCCOPY))
   {
      printf("Error!!!\n");
      return;
   }

   // Call GetDIBits a second time, this time to (format and) store the actual
   // bitmap data (the "pixels") in the buffer lpPixels
   lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];
   if (0 == GetDIBits(hDC, hBitmap, 0, MyBMInfo.bmiHeader.biHeight, lpPixels, &MyBMInfo, DIB_RGB_COLORS))
   {
      printf("Error!!! detectScreenEdges:GetDIBits 2 failed\n");
      return;
   }

   //Find top edge
   for (y = 0; y < res.height; y++)
   {
      unsigned int inv_y = res.height - 1 - y;
      sum = 0;
      valCount = 0;
      x_add = inv_y * res.width * NUM_VALUES_PER_WIN_PIXEL;

      for (x = 0; x < (res.width * NUM_VALUES_PER_WIN_PIXEL); x++)
      {
         if (x % NUM_VALUES_PER_WIN_PIXEL == leds::numValuesPerPixel)
            continue; //Don't count the alpha channel

         sum += lpPixels[x + x_add];
         valCount++;
      }

      precession = (double)sum / valCount / MAXBYTE;
      if (precession > edgeDetection.sensitivity)
      {
         newTopEdge = y;
         //if (gCurEdges.top != newTopEdge) printf("Detected new top edge - %d, precession - %f\n", newTopEdge, precession);

         break;
      }
   }

   //Find bottom edge
   for (y = 0; y < res.height; y++)
   {
      sum = 0;
      x_add = y * res.width * NUM_VALUES_PER_WIN_PIXEL;

      for (x = 0; x < (res.width * NUM_VALUES_PER_WIN_PIXEL); x++)
      {
         if (x % NUM_VALUES_PER_WIN_PIXEL == leds::numValuesPerPixel)
            continue; //Don't count the alpha channel
         sum += lpPixels[x + x_add];
      }

      precession = (double)sum / valCount / MAXBYTE;
      if (precession > edgeDetection.sensitivity)
      {
         newBottomEdge = res.height - y - 1;
         //if (gCurEdges.bottom != newBottomEdge) printf("Detected new bottom edge - %d, precession - %f\n", newBottomEdge, precession);

         break;
      }
   }

   //Find left edge
   for (x = 0; x < res.width; x++)
   {
      unsigned int pixel_x;
      sum = 0;
      valCount = 0;

      //no need to count the Alpha channel
      for (pixel_x = 0; pixel_x < leds::numValuesPerPixel; pixel_x++)
      {
         unsigned int final_pixel_x = (x * NUM_VALUES_PER_WIN_PIXEL) + pixel_x;
         for (y = 0; y < res.height; y++)
         {
            x_add = y * res.width * NUM_VALUES_PER_WIN_PIXEL;

            sum += lpPixels[final_pixel_x + x_add];
            valCount++;
         }
      }

      precession = (double)sum / valCount / MAXBYTE;
      if (precession > edgeDetection.sensitivity)
      {
         newLeftEdge = x;
         //if (gCurEdges.left != newLeftEdge) printf("Detected new left edge - %d, precession - %f\n", newLeftEdge, precession);

         break;
      }
   }

   //Find right edge
   for (x = 0; x < res.width; x++)
   {
      unsigned int inv_x = res.width - 1 - x;
      unsigned int pixel_x;
      sum = 0;
      valCount = 0;

      //no need to count the Alpha channel
      for (pixel_x = 0; pixel_x < leds::numValuesPerPixel; pixel_x++)
      {
         unsigned int final_pixel_x = (inv_x * NUM_VALUES_PER_WIN_PIXEL) + pixel_x;
         for (y = 0; y < res.height; y++)
         {
            x_add = y * res.width * NUM_VALUES_PER_WIN_PIXEL;

            sum += lpPixels[final_pixel_x + x_add];
            valCount++;
         }
      }

      precession = (double)sum / valCount / MAXBYTE;
      if (precession > edgeDetection.sensitivity)
      {
         newRightEdge = inv_x;
         //if (gCurEdges.right != newRightEdge) printf("Detected new right edge - %d, precession - %f\n", newRightEdge, precession);
         
         break;
      }
   }


   // clean up
   delete[] lpPixels;
   DeleteObject(hBitmap);
   DeleteDC(hDC);
   ReleaseDC(NULL, hScreen);

   if ((newTopEdge >= res.height) || (newBottomEdge > res.height) || (newTopEdge >= newBottomEdge)
      || (newRightEdge >= res.width) || (newLeftEdge > res.width) || (newLeftEdge >= newRightEdge))
   {
      printf("Error!!! screen edges value is illegal, discarding values\n");
      return;
   }
   
   //Update top
   if (newTopEdge != curEdges.top)
   if ((!edgeDetection.stabilityEnable) || (newTopEdge == edgeDetection.suggestedEdges.top))
         curEdges.top = newTopEdge;
      else
         edgeDetection.suggestedEdges.top = newTopEdge;
   
   //Update bottom
      if (newBottomEdge != curEdges.bottom)
      if ((!edgeDetection.stabilityEnable) || (newBottomEdge == edgeDetection.suggestedEdges.bottom))
         curEdges.bottom = newBottomEdge;
      else
         edgeDetection.suggestedEdges.bottom = newBottomEdge;
   
   //Update left
      if (newLeftEdge != curEdges.left)
      if ((!edgeDetection.stabilityEnable) || (newLeftEdge == edgeDetection.suggestedEdges.left))
         curEdges.left = newLeftEdge;
      else
         edgeDetection.suggestedEdges.left = newLeftEdge;
   
   //Update right
      if (newRightEdge != curEdges.right)
      if ((!edgeDetection.stabilityEnable) || (newRightEdge == edgeDetection.suggestedEdges.right))
         curEdges.right = newRightEdge;
      else
         edgeDetection.suggestedEdges.right = newRightEdge;
}

// Translate windows pixel format (BGRA) to WS2812b format (RGB)
inline void translateWin2LedPixel(const BYTE* winPixel, BYTE* ledPixel, const float brightnessNormalizationCoef)
{
   int red, green, blue;
   float bCoef = gLeds.getBrightnessCoef();

   //alpha = lpPixels[pixel + 3];
   red = *(winPixel + 2);
   green = *(winPixel + 1);
   blue = *(winPixel + 0);
   
   *(ledPixel + 0) = (BYTE)(red   * bCoef * brightnessNormalizationCoef);
   *(ledPixel + 1) = (BYTE)(green * bCoef * brightnessNormalizationCoef);
   *(ledPixel + 2) = (BYTE)(blue  * bCoef * brightnessNormalizationCoef);
}

void prepareLedColors(BYTE *finalPixals, const BYTE* lpPixels, const BITMAPINFO *MyBMInfo)
{
   int x, y, x_add; // Note: x,y must be signed, some loops are depended on it
   int pixel, finalPixel;
   float brightnessNormalizationCoef;

   int stride = MyBMInfo->bmiHeader.biWidth;
   finalPixel = 0;

   //Bottom side
   x = 0; y = 0;
   x_add = y * stride;
   for (x = 0; x < (int)leds::numHorisontal; x++)
   {
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((x == 0) || (x == leds::numHorisontal - 1))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);

      finalPixel += leds::numValuesPerPixel;
   }

   //Right side
   x = leds::numHorisontal - 1;
   for (y = 0; y < (int)leds::numVertical; y++)
   {
      x_add = y * stride;
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((y == 0) || (y == (leds::numVertical - 1)))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);

      finalPixel += leds::numValuesPerPixel;
   }

   //Top side
   y = leds::numVertical - 1;
   x_add = y * stride;
   for (x = leds::numHorisontal - 1; x >= 0; x--)
   {
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((x == 0) || (x == leds::numHorisontal - 1))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);

      finalPixel += leds::numValuesPerPixel;
   }

   //Left side
   x = 0;
   for (y = leds::numVertical - 1; y >= 0; y--)
   {
      x_add = y * stride;
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((y == 0) || (y == (leds::numVertical - 1)))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);

      finalPixel += leds::numValuesPerPixel;
   }
}

void captureLoop()
{
   // copy screen to bitmap
   HDC     hScreen = GetDC(NULL);
   HDC     hDC = CreateCompatibleDC(hScreen);
   HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, leds::numHorisontal, leds::numVertical);

   SelectObject(hDC, hBitmap);

   BITMAPINFO MyBMInfo = { 0 };

   MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

   // Get the BITMAPINFO structure from the bitmap
   if (0 == GetDIBits(hDC, hBitmap, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS))
   {
      // error handling
      printf("Error!!! GetDIBits (initial) failed\n");
      gLeds.clearLeds();
      gExitProgram = TRUE;
   }

   MyBMInfo.bmiHeader.biBitCount = 32;
   MyBMInfo.bmiHeader.biCompression = BI_RGB;  // no compression -> easier to use
   MyBMInfo.bmiHeader.biHeight = abs(MyBMInfo.bmiHeader.biHeight); // correct the bottom-up ordering of lines

   // create the pixel buffer
   BYTE* lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];
   BYTE* finalPixals = new BYTE[leds::totalNumBytesToSend];

   while (!gExitProgram)
   {
      if (!gLeds.isConnected())
      {
         Sleep(100);
         continue;
      }

      if ((gScreen.curEdges.top >= gScreen.res.height) || (gScreen.curEdges.bottom >= gScreen.res.height) || (gScreen.curEdges.top >= gScreen.curEdges.bottom)
         || (gScreen.curEdges.right >= gScreen.res.width) || (gScreen.curEdges.left > gScreen.res.width) || (gScreen.curEdges.left >= gScreen.curEdges.right))
      {
         printf("Error!!! wrong edges...\n");

         gLeds.clearLeds();
         gExitProgram = TRUE;
         break;
      }

      SetStretchBltMode(hDC, HALFTONE);
      BOOL bRet = StretchBlt(hDC, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hScreen, gScreen.curEdges.left, gScreen.curEdges.top, (gScreen.curEdges.right - gScreen.curEdges.left + 1), (gScreen.curEdges.bottom - gScreen.curEdges.top + 1), SRCCOPY);
      if (!bRet)
      {
         printf("Error!!! StretchBlt failed\n");

         gLeds.clearLeds();
         gExitProgram = TRUE;
         break;
      }

      /*
      // save bitmap to clipboard
      OpenClipboard(NULL);
      EmptyClipboard();
      SetClipboardData(CF_BITMAP, hBitmap);
      CloseClipboard();
      */

      // Call GetDIBits a second time, this time to (format and) store the actual
      // bitmap data (the "pixels") in the buffer lpPixels
      if (0 == GetDIBits(hDC, hBitmap, 0, MyBMInfo.bmiHeader.biHeight, lpPixels, &MyBMInfo, DIB_RGB_COLORS))
      {
         // error handling
         printf("Error!!! GetDIBits failed\n");

         gLeds.clearLeds();
         gExitProgram = TRUE;
         break;
      }

      // prepare all LED colors
      prepareLedColors(finalPixals, lpPixels, &MyBMInfo);

      gLeds.setLeds(finalPixals, leds::totalNumBytesToSend);
      
      Sleep(1); // Sleep 1mSec just to yield the thread
   }

   printf("Capture loop is finished...\n");

   // clean up
   delete[] lpPixels;
   delete[] finalPixals;
   DeleteObject(hBitmap);
   DeleteDC(hDC);
   ReleaseDC(NULL, hScreen);

   gLeds.clearLeds();
}

void leds::runLedTest()
{
   setSolidColor(255, 0, 0);
   Sleep(1000);
   setSolidColor(0, 255, 0);
   Sleep(1000);
   setSolidColor(0, 0, 255);
   Sleep(1000);
   clearLeds();
   Sleep(1000);
}

BOOL CtrlHandler(DWORD fdwCtrlType)
{
   gExitProgram = TRUE;
   return TRUE;
}

///////////////////////////////////////////////////////////////////////////////////
// Thread routines
///////////////////////////////////////////////////////////////////////////////////

DWORD WINAPI detectScreenEdgesThread(LPVOID lpParam)
{
   printf("Screen edge detection thread started - detection interval is %d [Sec]\n", gEdgeDetectionCheckIntervalSec);

   while (!gExitProgram)
   {
      if (gLeds.isConnected())
      {
         gScreen.res.update();
         if (edgeDetection_enable)
         {
            gScreen.detectEdges();
         }
         else
         {
            gScreen.setDefaultEdges();
         }
      }

      Sleep(gEdgeDetectionCheckIntervalSec * SEC_TO_MSEC);
   }

   return 0;
}

DWORD WINAPI captureThread(LPVOID lpParam)
{
   captureLoop();
   return 0;
}

DWORD WINAPI serialConnectionThread(LPVOID lpParam)
{
   BOOL allowFastReconnect = FALSE;

   while (!gExitProgram)
   {
      if (!gLeds.isConnected())
      {
         gLeds.tryConnect(!allowFastReconnect);

         allowFastReconnect = FALSE;
      }
      else
      {
         allowFastReconnect = TRUE;
      }

      Sleep(500);
   }

   return 0;
}

HANDLE startThread(LPTHREAD_START_ROUTINE threadRoutine)
{
   DWORD dwThreadId;
   HANDLE hThread = CreateThread(
      NULL,                      // default security attributes
      0,                         // use default stack size
      threadRoutine,             // thread function
      NULL,                      // argument to thread function
      0,                         // use default creation flags
      &dwThreadId);              // returns the thread identifier

   if (hThread == NULL)
      printf("Thread failed, error: %d\n", GetLastError());
   else
      printf("Thread started... (ID %d)\n", dwThreadId);

   return hThread;
}

///////////////////////////////////////////////////////////////////////////////////
// Entry-point
///////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
   HANDLE hThreadSerial, hThreadEdges, hThreadCapture;

   if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
   {
      printf("ERROR: could not set control handler.\n");
      return -1;
   }

   // Start threads
   hThreadSerial  = startThread(serialConnectionThread);
   hThreadEdges   = startThread(detectScreenEdgesThread);
   hThreadCapture = startThread(captureThread);

   // Program termination
   printf("Press any key to terminate...\n");
   getchar();
   gExitProgram = TRUE;
   printf("Terminating execution\n");

   // wait for the threads to terminate
   WaitForSingleObject(hThreadCapture, INFINITE);
   CloseHandle(hThreadCapture);

   WaitForSingleObject(hThreadSerial, INFINITE);
   CloseHandle(hThreadSerial);

   WaitForSingleObject(hThreadEdges, INFINITE);
   CloseHandle(hThreadEdges);
   
   return 0;
}


