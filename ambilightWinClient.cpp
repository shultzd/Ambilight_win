// ambilightServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdlib.h>
#include <windows.h>

///////////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////////
#define NUM_VALUES_PER_WIN_PIXEL   (4)
#define NUM_VALUES_PER_STRIP_PIXEL (3)

///////////////////////////////////////////////////////////////////////////////////
// Parameters
///////////////////////////////////////////////////////////////////////////////////

//COM port
#define SERIAL_COM_PORT L"\\\\.\\COM1"

//General config
#define BRIGHTNESS_COEF                  (1.0)
#define SCREEN_EDGE_DETECT_INTERVAL_SEC  (10)   //Time interval between screen edge detection
#define EDGE_DETECT_SENSITIVITY          (0.05) // Edge detection sensitivity 0-1 (0% - 100%)
#define EDGE_DETECT_STABILITY            TRUE   // do the 2 stage edge detection?

//Resolution of LEDs
#define LEDS_WIDTH  (30)
#define LEDS_HEIGHT (15)

#define LEDS_TOTAL_AMOUNT ((LEDS_WIDTH + LEDS_HEIGHT) * 2) // Amount of LEDs on all 4 sides
#define TOTAL_NUMBER_OF_BYTES_TO_SEND (LEDS_TOTAL_AMOUNT * NUM_VALUES_PER_STRIP_PIXEL)

///////////////////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////////////////

BOOL stopCapturing = 0;

int gSuggestedTopEdge = MAXINT32;
int gSuggestedBottomEdge = MAXINT32;

int gTopEdge = MAXINT32;
int gBottomEdge = MAXINT32;

BOOL setupSerialComm(_In_ LPCWSTR portName, _Out_ HANDLE &hSerial)
{
   DCB dcbSerialParams = { 0 };
   COMMTIMEOUTS timeouts = { 0 };

   // Open the highest available serial port number
   printf("Opening serial port...");
   hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   if (hSerial == INVALID_HANDLE_VALUE)
   {
      printf("Error\n");
      return FALSE;
   }
   else printf("OK\n");

   // Set device parameters (115200 baud, 1 start bit,
   // 1 stop bit, no parity)
   dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
   if (GetCommState(hSerial, &dcbSerialParams) == 0)
   {
      printf("Error getting device state\n");
      CloseHandle(hSerial);
      return FALSE;
   }

   dcbSerialParams.BaudRate = CBR_115200;
   dcbSerialParams.ByteSize = 8;
   dcbSerialParams.StopBits = ONESTOPBIT;
   dcbSerialParams.Parity = NOPARITY;
   if (SetCommState(hSerial, &dcbSerialParams) == 0)
   {
      printf("Error setting device parameters\n");
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
      printf("Error setting timeouts\n");
      CloseHandle(hSerial);
      return FALSE;
   }

   printf("Serial port initialization passed.\n");
   return TRUE;
}

BOOL sendToArduino(HANDLE &hSerial, BYTE *finalPixels, int numPixels)
{
   const BYTE preamble[5] = { 'd', 'a', 'n', 'n', 'y' };
   const BYTE ackValue = 'k';

   const int preambleSize = 5;
   const int ackSize = 1;

   // Send preamble
   DWORD bytes_written;
   if (!WriteFile(hSerial, preamble, preambleSize, &bytes_written, NULL))
   {
      printf("Error!!! sendToArduino:WriteFile 1 failed\n");
      CloseHandle(hSerial);
      return FALSE;
   }

   // Send specified text (remaining command line arguments)
   if (!WriteFile(hSerial, finalPixels, numPixels, &bytes_written, NULL))
   {
      printf("Error!!! sendToArduino:WriteFile 2 failed\n");
      CloseHandle(hSerial);
      return FALSE;
   }

   //Wait for a response
   BYTE ackByte = 0;
   DWORD bytes_read = 0;
   do
   {
      if (!ReadFile(hSerial, &ackByte, ackSize, &bytes_read, NULL))
      {
         printf("Error!!! sendToArduino:ReadFile failed\n");
         CloseHandle(hSerial);
         return FALSE;
      }
   } while (bytes_read == 0);

   if (ackByte != ackValue)
   {
      printf("Error!!! sendToArduino:ReadFile failed !! wrong ACK byte received...\n");
      CloseHandle(hSerial);
      return FALSE;
   }

   return TRUE;
}

void setSolidColor(HANDLE &hSerial, const BYTE red, const BYTE green, const BYTE blue)
{
   BYTE finalPixals[TOTAL_NUMBER_OF_BYTES_TO_SEND] = { 0 };
   int i = 0;

   for (i = 0; i < TOTAL_NUMBER_OF_BYTES_TO_SEND; i += NUM_VALUES_PER_STRIP_PIXEL)
   {
      finalPixals[i + 0] = red;
      finalPixals[i + 1] = green;
      finalPixals[i + 2] = blue;
   }

   sendToArduino(hSerial, finalPixals, TOTAL_NUMBER_OF_BYTES_TO_SEND);
}

inline void clearLeds(HANDLE &hSerial)
{
   setSolidColor(hSerial, 0, 0, 0);
}

void detectScreenEdges()
{
   int width = GetSystemMetrics(SM_CXSCREEN);
   int height = GetSystemMetrics(SM_CYSCREEN);

   int newTopEdge = gTopEdge;
   int newBottomEdge = gBottomEdge;

   if ((gTopEdge == MAXINT32) || (gBottomEdge == MAXINT32))
   {
      printf("Initial screen edge detection, setting default values\n");
      gTopEdge = 0;
      gBottomEdge = height - 1;
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
      if (precession > EDGE_DETECT_SENSITIVITY)
      {
         newTopEdge = y;
         //if (gTopEdge != newTopEdge) printf("Detected new top edge - %d, precession - %f\n", newTopEdge, precession);
         
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
      if (precession > EDGE_DETECT_SENSITIVITY)
      {
         newBottomEdge = height - y - 1;
         //if (gBottomEdge != newBottomEdge) printf("Detected new bottom edge - %d, precession - %f\n", newBottomEdge, precession);

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

   if ((newTopEdge != gTopEdge) || (newBottomEdge != gBottomEdge))
   {
      if ((!EDGE_DETECT_STABILITY) || ((newTopEdge == gSuggestedTopEdge) && (newBottomEdge == gSuggestedBottomEdge)))
      {
         //printf("Setting new screen edges\n");
         gTopEdge = newTopEdge;
         gBottomEdge = newBottomEdge;
      }
      else
      {
         //printf("Suggesting new screen edges\n");
         gSuggestedTopEdge = newTopEdge;
         gSuggestedBottomEdge = newBottomEdge;
      }
   }
}

// Translate windows pixel format (BGRA) to WS2812b format (RGB)
inline void translateWin2LedPixel(_In_ const BYTE* winPixel, _Out_ BYTE* ledPixel)
{
   int red, green, blue;

   //alpha = lpPixels[pixel + 3];
   red   = *(winPixel + 2);
   green = *(winPixel + 1);
   blue  = *(winPixel + 0);

   *(ledPixel + 0) = (BYTE)(red   * BRIGHTNESS_COEF);
   *(ledPixel + 1) = (BYTE)(green * BRIGHTNESS_COEF);
   *(ledPixel + 2) = (BYTE)(blue  * BRIGHTNESS_COEF);
}

void prepareLedColors(_Out_ BYTE *finalPixals, _In_ BYTE* lpPixels, BITMAPINFO *MyBMInfo)
{
   int x, y, x_add;
   int pixel, finalPixel;

   int stride = MyBMInfo->bmiHeader.biWidth;
   finalPixel = 0;

   //Bottom side
   x = 0; y = 0;
   x_add = y * stride;
   for (x = 0; x < LEDS_WIDTH; x++)
   {
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;
      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel]);
      
      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }

   //Right side
   x = LEDS_WIDTH - 1;
   for (y = 0; y < LEDS_HEIGHT; y++)
   {
      x_add = y * stride;
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;

      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel]);

      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }

   //Top side
   y = LEDS_HEIGHT - 1;
   x_add = y * stride;
   for (x = LEDS_WIDTH - 1; x >= 0; x--)
   {
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;
      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel]);

      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }

   //Left side
   x = 0;
   for (y = LEDS_HEIGHT - 1; y >= 0; y--)
   {
      x_add = y * stride;
      pixel = (x + x_add) * NUM_VALUES_PER_WIN_PIXEL;
      translateWin2LedPixel(&lpPixels[pixel], &finalPixals[finalPixel]);

      finalPixel += NUM_VALUES_PER_STRIP_PIXEL;
   }

}

void captureLoop(HANDLE &hSerial)
{
   int width = GetSystemMetrics(SM_CXSCREEN);
   int height = GetSystemMetrics(SM_CYSCREEN);

   printf("screen is %dx%d\n", width, height);

   // copy screen to bitmap
   HDC     hScreen = GetDC(NULL);
   HDC     hDC = CreateCompatibleDC(hScreen);
   HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, LEDS_WIDTH, LEDS_HEIGHT);

   SelectObject(hDC, hBitmap);

   BITMAPINFO MyBMInfo = { 0 };

   MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

   // Get the BITMAPINFO structure from the bitmap
   if (0 == GetDIBits(hDC, hBitmap, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS))
   {
      // error handling
      printf("Error!!! GetDIBits (initial) failed\n");
      stopCapturing = TRUE;
   }

   MyBMInfo.bmiHeader.biBitCount = 32;
   MyBMInfo.bmiHeader.biCompression = BI_RGB;  // no compression -> easier to use
   MyBMInfo.bmiHeader.biHeight = abs(MyBMInfo.bmiHeader.biHeight); // correct the bottom-up ordering of lines

   // create the pixel buffer
   BYTE* lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];
   BYTE finalPixals[TOTAL_NUMBER_OF_BYTES_TO_SEND];

   while (!stopCapturing)
   {
      if ((gTopEdge >= height) || (gBottomEdge >= height) || (gTopEdge >= gBottomEdge))
      {
         printf("Error!!! wrong edges...\n");
         stopCapturing = TRUE;
         break;
      }

      SetStretchBltMode(hDC, HALFTONE);
      BOOL bRet = StretchBlt(hDC, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hScreen, 0, gTopEdge, width, (gBottomEdge - gTopEdge + 1), SRCCOPY);
      if (!bRet)
      {
         printf("Error!!! StretchBlt failed\n");
         stopCapturing = TRUE;
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
         stopCapturing = TRUE;
         break;
      }

      // prepare all LED colors
      prepareLedColors(finalPixals, lpPixels, &MyBMInfo);
     
      if (!sendToArduino(hSerial, finalPixals, TOTAL_NUMBER_OF_BYTES_TO_SEND))
      {
         printf("Error!!! sendToArduino failed\n");
         printf("Sending data to serial port failed!\n");
         stopCapturing = TRUE;
         break;
      }

      Sleep(1); // Sleep 1mSec just to yield the thread
   }

   printf("Capture loop is finished...\n");

   // clean up
   delete[] lpPixels;
   DeleteObject(hBitmap);
   DeleteDC(hDC);
   ReleaseDC(NULL, hScreen);

   clearLeds(hSerial);
}

DWORD WINAPI detectScreenEdgesThread(LPVOID lpParam)
{
   const int SEC = 1000; // 1 second

   printf("Screen edge detection thread started - detection interval is %d [Sec]\n", SCREEN_EDGE_DETECT_INTERVAL_SEC);

   for (;;)
   {
      if (!stopCapturing)
      {
         detectScreenEdges();
      }

      Sleep(SCREEN_EDGE_DETECT_INTERVAL_SEC * SEC);
   }

   return 0;
}

void StartDetectScreenEdgesThread()
{
   DWORD dwThreadId;
   HANDLE hThread = CreateThread(
      NULL,                      // default security attributes
      0,                         // use default stack size
      detectScreenEdgesThread,   // thread function
      NULL,                      // argument to thread function
      0,                         // use default creation flags
      &dwThreadId);              // returns the thread identifier

   if (hThread == NULL)
      printf("Detect screen edges thread failed, error: %d\n", GetLastError());
   else
      printf("Detect screen edges thread started... (ID %d)\n", dwThreadId);

   if (CloseHandle(hThread) != 0)
      printf("Handle to thread closed successfully.\n");
}

DWORD WINAPI captureThread(LPVOID lpParam)
{
   HANDLE *hSerial = (HANDLE*)lpParam;
   captureLoop(*hSerial);
   return 0;
}

void StartCaptureThread(HANDLE &hSerial)
{
   DWORD dwThreadId;
   HANDLE hThread = CreateThread(
      NULL,                      // default security attributes
      0,                         // use default stack size
      captureThread,             // thread function
      &hSerial,                  // argument to thread function
      0,                         // use default creation flags
      &dwThreadId);              // returns the thread identifier

   if (hThread == NULL)
      printf("Capture screen thread failed, error: %d\n", GetLastError());
   else
      printf("Capture screen thread started... (ID %d)\n", dwThreadId);

   if (CloseHandle(hThread) != 0)
      printf("Handle to thread closed successfully.\n");
}

BOOL CtrlHandler(DWORD fdwCtrlType)
{
   stopCapturing = TRUE;
   return TRUE;
}

int main(int argc, char* argv[])
{
   HANDLE hSerial;
   
   if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
   {
      printf("ERROR: could not set control handler.\n");
      return -1;
   }
   
   detectScreenEdges(); //Do the detection for the first time
   
   if (!setupSerialComm(SERIAL_COM_PORT, hSerial))
   {
      printf("Error connecting to COM port\n");
      CloseHandle(hSerial);
      return -1;
   }

   clearLeds(hSerial);

   printf("starting the edge detection thread\n");
   StartDetectScreenEdgesThread();
   
   printf("starting the capturing loop thread\n");
   StartCaptureThread(hSerial);

   // Program termination
   getchar();
   stopCapturing = TRUE;
   printf("Terminating execution\n");
   Sleep(1000); //Sleep 1Sec to give the threads time to terminate

   // Close serial port
   printf("Closing serial port...");
   if (CloseHandle(hSerial) == 0)
   {
      printf("Error\n");
      return -1;
   }

   printf("All done\n");

   return 0;
}


