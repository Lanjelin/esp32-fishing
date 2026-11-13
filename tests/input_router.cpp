#include "../TwoButtonPad/InputRouter.h"
#include <cassert>
int main() {
  InputRouter a;
  assert(!a.update(0,true,false) && a.output == 0);
  assert(a.update(60,true,true) && a.output == 0);
  assert(!a.update(200,true,true) && a.output == 0);
  assert(!a.update(220,false,true) && a.output == 0);
  assert(!a.update(240,true,true) && a.output == 0);
  a.update(260,false,false);
  assert(a.update(280,true,true) && a.output == 0);
  a.update(300,false,false);
  a.update(400,true,false);
  assert(!a.update(499,true,false) && a.output == 0);
  assert(!a.update(500,true,false) && a.output == 1);
  a.update(510,false,false); assert(a.output == 0);
  a.update(600,false,true);
  a.update(630,false,false); assert(a.output == 2);
  a.update(649,false,false); assert(a.output == 2);
  a.update(650,false,false); assert(a.output == 0);
  // A second quick single tap is queued rather than lost during the first pulse.
  a.update(700,true,false);
  a.update(730,false,false); assert(a.output == 1);
  a.update(735,false,true);
  a.update(740,false,false);
  a.update(750,false,false); assert(a.output == 0);
  a.update(770,false,false); assert(a.output == 2);
  a.update(790,false,false); assert(a.output == 0);
  // A later second press never produces a simultaneous HID report either.
  a.update(800,true,false); a.update(900,true,false); assert(a.output == 1);
  assert(a.update(950,true,true) && a.output == 0);
  a.update(960,false,false);
  a.update(UINT32_MAX-50,true,false);
  a.update(49,true,false); assert(a.output == 1);
  InputRouter b;
  b.update(0,true,false); b.update(30,false,false);
  b.update(35,false,true);
  assert(b.update(40,true,true) && b.output == 0);
  b.update(50,false,false);
  b.update(60,true,false); b.update(80,false,false);
  b.update(100,false,false); assert(b.output == 0);
  b.update(120,false,false); assert(b.output == 0); // no stale queued Cast
  InputRouter c;
  c.update(0,true,false); c.update(30,false,false); assert(c.output == 1);
  c.update(35,true,false); c.update(45,false,false);
  c.update(50,false,false); assert(c.output == 0);
  c.update(70,false,false); assert(c.output == 1); // distinct repeated Reel tap
  c.update(90,false,false); assert(c.output == 0);
}
