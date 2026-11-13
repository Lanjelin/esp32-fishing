#include "../TwoButtonPad/InputRouter.h"
#include <cassert>

int main() {
  // Every selectable duration, for a short tap and the former 5ms edge case.
  for (uint32_t duration = 60; duration <= 80; ++duration) {
    InputRouter tap;
    tap.update(0,true,false,duration);
    tap.update(40,false,false,duration); assert(tap.output == 1);
    tap.noteReportSent(50); // submission delayed by work in the main loop
    tap.update(50+duration-1,false,false,60); assert(tap.output == 1);
    tap.noteReportSent(50+duration-1); // refresh must NOT restart the minimum
    tap.update(50+duration,false,false,80); assert(tap.output == 0);

    InputRouter edge;
    edge.update(0,true,false,duration);
    edge.update(100,true,false,duration); assert(edge.output == 1);
    edge.noteReportSent(100);
    edge.update(105,false,false,duration); assert(edge.output == 1);
    edge.update(100+duration-1,false,false,duration); assert(edge.output == 1);
    edge.update(100+duration,false,false,duration); assert(edge.output == 0);
  }
  InputRouter hold;
  hold.update(0,false,true,60); hold.update(100,false,true,60);
  hold.noteReportSent(100);
  hold.update(1000,false,true,80); assert(hold.output == 2);
  hold.update(1001,false,false,80); assert(hold.output == 0);

  InputRouter chord;
  chord.update(0,true,false);
  assert(chord.update(60,true,true) && chord.output == 0);
  assert(!chord.update(300,true,true) && chord.output == 0);
  chord.update(310,false,true); assert(chord.output == 0);
  assert(!chord.update(320,true,true));
  chord.update(330,false,false);
  assert(chord.update(340,true,true) && chord.output == 0);

  // An overlapping second button never reaches HID; finish an existing minimum.
  InputRouter late;
  late.update(0,true,false,80); late.update(100,true,false,80);
  late.noteReportSent(100);
  assert(late.update(105,true,true,60) && late.output == 1);
  late.update(110,false,false,60); assert(late.output == 1);
  late.update(180,false,false,60); assert(late.output == 0);

  // A repeated quick tap has a separate release report and minimum of its own.
  InputRouter queue;
  queue.update(0,true,false,60); queue.update(30,false,false,60);
  queue.noteReportSent(30);
  queue.update(40,true,false,80); queue.update(60,false,false,80);
  queue.update(90,false,false,80); assert(queue.output == 0);
  queue.update(109,false,false,80); assert(queue.output == 0);
  queue.update(110,false,false,80); assert(queue.output == 1);
  queue.noteReportSent(110);
  queue.update(189,false,false,60); assert(queue.output == 1);
  queue.update(190,false,false,60); assert(queue.output == 0);

  InputRouter wrap;
  wrap.update(UINT32_MAX-200,true,false,60);
  wrap.update(UINT32_MAX-100,true,false,60);
  wrap.noteReportSent(UINT32_MAX-20);
  wrap.update(38,false,false,60); assert(wrap.output == 1);
  wrap.update(39,false,false,60); assert(wrap.output == 0);
}
