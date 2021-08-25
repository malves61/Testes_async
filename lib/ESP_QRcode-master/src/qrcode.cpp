#include <Arduino.h>
#include "qrcode.h"
#include "qrencode.h"

int offsetsX = 10;
int offsetsY = 10;
int screenwidth = 128;
int screenheight = 64;

bool QRDEBUG = false;
int multiply = 1;

QRcode::QRcode(GxDEPG0213BN *eink){
	this->eink = eink;
  offsetsX = 20;
  offsetsY = 20;
}

void QRcode::init(){
      screenwidth = eink->width();
      screenheight = eink->height();
      int min = screenwidth;
      if (screenheight<screenwidth)
        min = screenheight;
      multiply = min/WD;
      offsetsX = (screenwidth-(WD*multiply))/2;
      offsetsY = 11;
}

void QRcode::render(int x, int y, int color){
  
  multiply = 2;
  x=(x*multiply)+offsetsX;
  y=(y*multiply)+offsetsY;
  if(color==1) {
        eink->drawPixel(x,y,GxEPD_BLACK);
        if (multiply>1) {
          eink->drawPixel(x+1,y,GxEPD_BLACK);
          eink->drawPixel(x+1,y+1,GxEPD_BLACK);
          eink->drawPixel(x,y+1,GxEPD_BLACK);
        }  
  }
  else {
    eink->drawPixel(x,y,GxEPD_WHITE);
    if (multiply>1) {
      eink->drawPixel(x+1,y,GxEPD_WHITE);
      eink->drawPixel(x+1,y+1,GxEPD_WHITE);
      eink->drawPixel(x,y+1,GxEPD_WHITE);
    }  
  }
}

void QRcode::create(String message) {

  // create QR code
  message.toCharArray((char *)strinbuf,260);
  qrencode();
 
  // print QR Code
  for (byte x = 0; x < WD; x+=2) {
    for (byte y = 0; y < WD; y++) {
      if ( QRBIT(x,y) &&  QRBIT((x+1),y)) {
        // black square on top of black square
        render(x, y, 1);
        render((x+1), y, 1);
      }
      if (!QRBIT(x,y) &&  QRBIT((x+1),y)) {
        // white square on top of black square
        render(x, y, 0);
        render((x+1), y, 1);
      }
      if ( QRBIT(x,y) && !QRBIT((x+1),y)) {
        // black square on top of white square
        render(x, y, 1);
        render((x+1), y, 0);
      }
      if (!QRBIT(x,y) && !QRBIT((x+1),y)) {
        // white square on top of white square
        render(x, y, 0);
        render((x+1), y, 0);
      }
    }
  }
}
