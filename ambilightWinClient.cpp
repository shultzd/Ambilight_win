// ambilightServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdlib.h>
#include <windows.h>
#include <chrono> //For time measurements

///////////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////////

#define SERIAL_COM_PORT L"\\\\.\\COM1"

#define MSEC_TO_SEC (1000) // 1 mSec = 1000 uSec
#define USEC_TO_MSEC (1000) // 1 mSec = 1000 uSec

#define NUM_VALUES_PER_WIN_PIXEL   (4)
#define NUM_VALUES_PER_STRIP_PIXEL (3)

#define LEDS_TOTAL_AMOUNT ((gConfig.leds.numHorisontal + gConfig.leds.numVertical) * 2) // Amount of LEDs on all 4 sides
#define TOTAL_NUMBER_OF_BYTES_TO_SEND (LEDS_TOTAL_AMOUNT * NUM_VALUES_PER_STRIP_PIXEL)

///////////////////////////////////////////////////////////////////////////////////
// Configurations
///////////////////////////////////////////////////////////////////////////////////

typedef struct {
   unsigned int top;
   unsigned int bottom;
}screenEdges_t;

struct {
   float brightnessCoef;

   struct {
      bool enable;                   // Enable edge detection
      unsigned int checkIntervalSec; // Time interval between screen edge detection
      double sensitivity;            // Edge detection sensitivity 0-1 (0% - 100%)
      bool stabilityEnable;          // do the 2 stage edge detection?
   } edgeDetection;
   
   struct {
      unsigned int numHorisontal;
      unsigned int numVertical;
   } leds;

   HANDLE hSerial;
} gConfig = {0};

///////////////////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////////////////

// Flow control 
BOOL gIsSerialConnected = FALSE;
BOOL gExitProgram = FALSE;

// Screen edges
screenEdges_t gCurEdges = {MAXINT32, MAXINT32};
screenEdges_t gSuggestedEdges = {MAXINT32, MAXINT32};

void initConfig()
{
   gConfig.brightnessCoef = 1.0;

   gConfig.edgeDetection.enable                = TRUE;
   gConfig.edgeDetection.checkIntervalSec      = 5;
   gConfig.edgeDetection.sensitivity           = 0.01;
   gConfig.edgeDetection.stabilityEnable       = TRUE;
   
   gConfig.leds.numHorisontal = 28;
   gConfig.leds.numVertical   = 16;

   gConfig.hSerial = INVALID_HANDLE_VALUE;
}

BOOL setupSerialComm()
{
   DCB dcbSerialParams = { 0 };
   COMMTIMEOUTS timeouts = { 0 };

   // Open the highest available serial port number
   //printf("Opening serial port...");
   gConfig.hSerial = CreateFile(SERIAL_COM_PORT, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   if (gConfig.hSerial == INVALID_HANDLE_VALUE)
   {
      //printf("Error\n");
      return FALSE;
   }
   else
   {
      //printf("OK\n");
   }

   // Set device parameters (115200 baud, 1 start bit,
   // 1 stop bit, no parity)
   dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
   if (GetCommState(gConfig.hSerial, &dcbSerialParams) == 0)
   {
      //printf("Error getting device state\n");
      CloseHandle(gConfig.hSerial);
      return FALSE;
   }

   dcbSerialParams.BaudRate = CBR_115200;
   dcbSerialParams.ByteSize = 8;
   dcbSerialParams.StopBits = ONESTOPBIT;
   dcbSerialParams.Parity = NOPARITY;
   if (SetCommState(gConfig.hSerial, &dcbSerialParams) == 0)
   {
      //printf("Error setting device parameters\n");
      CloseHandle(gConfig.hSerial);
      return FALSE;
   }

   // Set COM port timeout settings
   timeouts.ReadIntervalTimeout = 50;
   timeouts.ReadTotalTimeoutConstant = 50;
   timeouts.ReadTotalTimeoutMultiplier = 10;
   timeouts.WriteTotalTimeoutConstant = 50;
   timeouts.WriteTotalTimeoutMultiplier = 10;
   if (SetCommTimeouts(gConfig.hSerial, &timeouts) == 0)
   {
      //printf("Error setting timeouts\n");
      CloseHandle(gConfig.hSerial);
      return FALSE;
   }

   //printf("Serial port initialization passed.\n");
   return TRUE;
}

BOOL sendToArduino(BYTE *finalPixels, int numPixels)
{
   const BYTE preamble[5] = { 'd', 'a', 'n', 'n', 'y' };
   const BYTE ackValue = 'k';

   const int preambleSize = 5;
   const int ackSize = 1;

   // Send preamble
   DWORD bytes_written;
   if (!WriteFile(gConfig.hSerial, preamble, preambleSize, &bytes_written, NULL))
   {
      //printf("Error!!! sendToArduino:WriteFile failed\n");
      CloseHandle(gConfig.hSerial);
      gIsSerialConnected = FALSE;
      return FALSE;
   }

   // Send specified text (remaining command line arguments)
   if (!WriteFile(gConfig.hSerial, finalPixels, numPixels, &bytes_written, NULL))
   {
      //printf("Error!!! sendToArduino:WriteFile failed\n");
      CloseHandle(gConfig.hSerial);
      gIsSerialConnected = FALSE;
      return FALSE;
   }

   //Wait for a response
   BYTE ackByte = 0;
   DWORD bytes_read = 0;
   auto start_time = std::chrono::high_resolution_clock::now();
   do
   {
      if (!ReadFile(gConfig.hSerial, &ackByte, ackSize, &bytes_read, NULL))
      {
         //printf("Error!!! sendToArduino:ReadFile failed\n");
         CloseHandle(gConfig.hSerial);
         gIsSerialConnected = FALSE;
         return FALSE;
      }

      auto end_time = std::chrono::high_resolution_clock::now();
      auto time = end_time - start_time;
      if ((std::chrono::duration_cast<std::chrono::microseconds>(time).count()) > (0.5 * USEC_TO_MSEC * MSEC_TO_SEC))
      {
         //printf("Error!!! sendToArduino: ACK timeout\n");
         CloseHandle(gConfig.hSerial);
         gIsSerialConnected = FALSE;
         return FALSE;
      }
   } while (bytes_read == 0);

   if (ackByte != ackValue)
   {
      //printf("Error!!! sendToArduino:ReadFile failed !! wrong ACK byte received...\n");
      CloseHandle(gConfig.hSerial);
      gIsSerialConnected = FALSE;
      return FALSE;
   }

   return TRUE;
}

BOOL setSolidColor(const BYTE red, const BYTE green, const BYTE blue)
{
   BOOL retVal;
   BYTE* finalPixals = new BYTE[TOTAL_NUMBER_OF_BYTES_TO_SEND];
   unsigned int i = 0;

   for (i = 0; i < TOTAL_NUMBER_OF_BYTES_TO_SEND; i += NUM_VALUES_PER_STRIP_PIXEL)
   {
      finalPixals[i + 0] = red;
      finalPixals[i + 1] = green;
      finalPixals[i + 2] = blue;
   }

   retVal = sendToArduino(finalPixals, TOTAL_NUMBER_OF_BYTES_TO_SEND);

   delete[] finalPixals;
   return retVal;
}

inline BOOL clearLeds()
{
   return setSolidColor(0, 0, 0);
}

void setDefaultScreenEdges()
{
   gCurEdges.top = 0;
   gCurEdges.bottom = GetSystemMetrics(SM_CYSCREEN) - 1;
}

void detectScreenEdges()
{
   int width = GetSystemMetrics(SM_CXSCREEN);
   int height = GetSystemMetrics(SM_CYSCREEN);

   int newTopEdge = gCurEdges.top;
   int newBottomEdge = gCurEdges.bottom;

   if ((gCurEdges.top == MAXINT32) || (gCurEdges.bottom == MAXINT32))
   {
      printf("Initial screen edge detection, setting default values\n");
      setDefaultScreenEdges();
   }

   // copy screen to bitmap
   HDC     hScreen = GetDC(NULL);
   HDC     hDC = CreateCompatibleDC(hScreen);
   HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);

   SelectObject(hDC, hBitmap);

   BITMAPINFO MyBMInfo = { 0 };

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

   BOOL bRet = BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);
   if (!bRet)
   {
      printf("Error!!!\n");
      return;
   }

   BYTE* lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];

   // Call GetDIBits a second time, this time to (format and) store the actual
   // bitmap data (the "pixels") in the buffer lpPixels
   if (0 == GetDIBits(hDC, hBitmap, 0, MyBMInfo.bmiHeader.biHeight, lpPixels, &MyBMInfo, DIB_RGB_COLORS))
   {
      printf("Error!!! detectScreenEdges:GetDIBits 2 failed\n");
      return;
   }

   int x, y, inv_y, x_add, valCount, sum;
   double precession;
   
   //Find top edge
   for (y = 0; y < height; y++)
   {
      sum = 0;
      valCount = 0;
      inv_y = height - 1 - y;
      x_add = inv_y * width * NUM_VALUES_PER_WIN_PIXEL;
      
      for (x = 0; x < (width * NUM_VALUES_PER_WIN_PIXEL); x++)
      {
         if (x % NUM_VALUES_PER_WIN_PIXEL == NUM_VALUES_PER_STRIP_PIXEL)
            continue; //Don't count the alpha channel

         sum += lpPixels[x + x_add];
         valCount++;
      }
      
      precession = (double)sum / valCount / MAXBYTE;
      if (precession > gConfig.edgeDetection.sensitivity)
      {
         newTopEdge = y;
         //if (gCurEdges.top != newTopEdge) printf("Detected new top edge - %d, precession - %f\n", newTopEdge, precession);
         
         break;
      }
   }

   //Find bottom edge
   for (y = 0; y < height; y++)
   {
      sum = 0;
      x_add = y * width * NUM_VALUES_PER_WIN_PIXEL;

      for (x = 0; x < (width * NUM_VALUES_PER_WIN_PIXEL); x++)
      {
         if (x % NUM_VALUES_PER_WIN_PIXEL == NUM_VALUES_PER_STRIP_PIXEL)
            continue; //Don't count the alpha channel
         sum += lpPixels[x + x_add];
      }

      precession = (double)sum / valCount / MAXBYTE;
      if (precession > gConfig.edgeDetection.sensitivity)
      {
         newBottomEdge = height - y - 1;
         //if (gCurEdges.bottom != newBottomEdge) printf("Detected new bottom edge - %d, precession - %f\n", newBottomEdge, precession);

         break;
      }
   }

   // clean up
   delete[] lpPixels;
   DeleteObject(hBitmap);
   DeleteDC(hDC);
   ReleaseDC(NULL, hScreen);

   if ((newTopEdge >= height) || (newBottomEdge > height) || (newTopEdge >= newBottomEdge))
   {
      printf("Error!!! screen edges value is illegal, discarding values\n");
      return;
   }

   if ((newTopEdge != gCurEdges.top) || (newBottomEdge != gCurEdges.bottom))
   {
      if ((!gConfig.edgeDetection.stabilityEnable) || ((newTopEdge == gSuggestedEdges.top) && (newBottomEdge == gSuggestedEdges.bottom)))
      {
         //printf("Setting new screen edges\n");
         gCurEdges.top = newTopEdge;
         gCurEdges.bottom = newBottomEdge;
      }
      else
      {
         //printf("Suggesting new screen edges\n");
         gSuggestedEdges.top = newTopEdge;
         gSuggestedEdges.bottom = newBottomEdge;
      }
   }
}

// Translate windows pixel format (BGRA) to WS2812b format (RGB)
inline void translateWin2LedPixel(const BYTE* winPixel, BYTE* ledPixel, const float brightnessNormalizationCoef)
{
   int red, green, blue;

   //alpha = lpPixels[pixel + 3];
   red   = *(winPixel + 2);
   green = *(winPixel + 1);
   blue  = *(winPixel + 0);

   *(ledPixel + 0) = (BYTE)(red   * gConfig.brightnessCoef * brightnessNormalizationCoef);
   *(ledPixel + 1) = (BYTE)(green * gConfig.brightnessCoef * brightnessNormalizationCoef);
   *(ledPixel + 2) = (BYTE)(blue  * gConfig.brightnessCoef * brightnessNormalizationCoef);
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
   for (x = 0; x < (int)gConfig.leds.numHorisontal; x++)
   {
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;
      
      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((x == 0) || (x == gConfig.leds.numHorisontal - 1))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);
      
      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }

   //Right side
   x = gConfig.leds.numHorisontal - 1;
   for (y = 0; y < (int)gConfig.leds.numVertical; y++)
   {
      x_add = y * stride;
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((y == 0) || (y == (gConfig.leds.numVertical - 1)))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);

      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }

   //Top side
   y = gConfig.leds.numVertical - 1;
   x_add = y * stride;
   for (x = gConfig.leds.numHorisontal - 1; x >= 0; x--)
   {
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((x == 0) || (x == gConfig.leds.numHorisontal - 1))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);

      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }

   //Left side
   x = 0;
   for (y = gConfig.leds.numVertical - 1; y >= 0; y--)
   {
      x_add = y * stride;
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      // Corner screen areas will light 2 leds, so I'm reducing brightness by 50% to compensate
      if ((y == 0) || (y == (gConfig.leds.numVertical - 1)))
         brightnessNormalizationCoef = 0.5;
      else
         brightnessNormalizationCoef = 1;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel], brightnessNormalizationCoef);

      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }
}

void captureLoop()
{
   unsigned int width = GetSystemMetrics(SM_CXSCREEN);
   unsigned int height = GetSystemMetrics(SM_CYSCREEN);

   printf("screen is %dx%d\n", width, height);

   // copy screen to bitmap
   HDC     hScreen = GetDC(NULL);
   HDC     hDC = CreateCompatibleDC(hScreen);
   HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, gConfig.leds.numHorisontal, gConfig.leds.numVertical);

   SelectObject(hDC, hBitmap);

   BITMAPINFO MyBMInfo = { 0 };

   MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

   // Get the BITMAPINFO structure from the bitmap
   if (0 == GetDIBits(hDC, hBitmap, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS))
   {
      // error handling
      printf("Error!!! GetDIBits (initial) failed\n");
      clearLeds();
      gExitProgram = TRUE;
   }

   MyBMInfo.bmiHeader.biBitCount = 32;
   MyBMInfo.bmiHeader.biCompression = BI_RGB;  // no compression -> easier to use
   MyBMInfo.bmiHeader.biHeight = abs(MyBMInfo.bmiHeader.biHeight); // correct the bottom-up ordering of lines

   // create the pixel buffer
   BYTE* lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];
   BYTE* finalPixals = new BYTE[TOTAL_NUMBER_OF_BYTES_TO_SEND];

   while (!gExitProgram)
   {
      if (!gIsSerialConnected)
      {
         Sleep(100);
         continue;
      }

      if ((gCurEdges.top >= height) || (gCurEdges.bottom >= height) || (gCurEdges.top >= gCurEdges.bottom))
      {
         printf("Error!!! wrong edges...\n");

         clearLeds();
         gExitProgram = TRUE;
         break;
      }

      SetStretchBltMode(hDC, HALFTONE);
      BOOL bRet = StretchBlt(hDC, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hScreen, 0, gCurEdges.top, width, (gCurEdges.bottom - gCurEdges.top + 1), SRCCOPY);
      if (!bRet)
      {
         printf("Error!!! StretchBlt failed\n");

         clearLeds();
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

         clearLeds();
         gExitProgram = TRUE;
         break;
      }

      // prepare all LED colors
      prepareLedColors(finalPixals, lpPixels, &MyBMInfo);
     
      if (!sendToArduino(finalPixals, TOTAL_NUMBER_OF_BYTES_TO_SEND))
      {
         printf("Error!!! sendToArduino failed\n");
         printf("Sending data to serial port failed!\n");
         continue;
      }

      Sleep(1); // Sleep 1mSec just to yield the thread
   }

   printf("Capture loop is finished...\n");

   // clean up
   delete[] lpPixels;
   delete[] finalPixals;
   DeleteObject(hBitmap);
   DeleteDC(hDC);
   ReleaseDC(NULL, hScreen);

   clearLeds();
}

BOOL runInitialLedTest()
{
   BOOL retVal;
   retVal = setSolidColor(255, 0, 0);
   if (retVal == FALSE) return FALSE;
   Sleep(500);
   retVal = setSolidColor(0, 255, 0);
   if (retVal == FALSE) return FALSE;
   Sleep(500);
   retVal = setSolidColor(0, 0, 255);
   if (retVal == FALSE) return FALSE;
   Sleep(500);
   retVal = clearLeds();
   if (retVal == FALSE) return FALSE;

   return TRUE;
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
   printf("Screen edge detection thread started - detection interval is %d [Sec]\n", gConfig.edgeDetection.checkIntervalSec);

   while (!gExitProgram)
   {
      if (gIsSerialConnected)
      {
         if (gConfig.edgeDetection.enable)
         {
            detectScreenEdges();
         }
         else
         {
            setDefaultScreenEdges();
         }
      }

      Sleep(gConfig.edgeDetection.checkIntervalSec * MSEC_TO_SEC);
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
   BOOL serialSetupPassed;
   BOOL testResult;

   while (!gExitProgram)
   {
      if (!gIsSerialConnected)
      {  
         serialSetupPassed = setupSerialComm();
         if (serialSetupPassed)
         {
            testResult = runInitialLedTest();
            if (testResult == TRUE)
            {
               gIsSerialConnected = TRUE;
            }
         }
      }

      Sleep(500);
   }
   
   return 0;
}

void startThread(LPTHREAD_START_ROUTINE threadRoutine)
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

   if (CloseHandle(hThread) != 0)
      printf("Handle to thread closed successfully.\n");
}

///////////////////////////////////////////////////////////////////////////////////
// Entry-point
///////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
   if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
   {
      printf("ERROR: could not set control handler.\n");
      return -1;
   }
   
   initConfig();
   setDefaultScreenEdges();

   printf("Starting serial connection thread\n");
   startThread(serialConnectionThread);

   printf("starting the edge detection thread\n");
   startThread(detectScreenEdgesThread);

   printf("starting the capturing loop thread\n");
   startThread(captureThread);
   
   // Program termination
   printf("Press any key to terminate...\n");
   getchar();
   gExitProgram = TRUE;
   printf("Terminating execution\n");
   Sleep((gConfig.edgeDetection.checkIntervalSec + 1) * MSEC_TO_SEC); //Give time for threads to terminate, edge detection needs the most time. 

   // Close serial port
   printf("Closing serial port...");
   if (CloseHandle(gConfig.hSerial) == 0)
   {
      printf("Error\n");
      return -1;
   }

   printf("All done\n");

   return 0;
}


