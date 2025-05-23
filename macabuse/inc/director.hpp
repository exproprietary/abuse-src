#ifndef __DIRECTOR_HPP_
#define __DIRECTOR_HPP_

#include "timing.hpp"

class director        // the director controlls the scene
{
  int tleft,ttop,tright,tbottom,text_y,text_step,
      pan_xv,pan_yv,pan_steps,
      frame_speed,scroll_speed,pan_speed,scene_abort;  
  char *text;
  time_marker *pan_time,*frame_time,*text_time;
public :
  void set_text_region(int left, int top, int right, int bottom) 
  { tleft=left; ttop=top; tright=right; tbottom=bottom; }
  void set_frame_speed(int speed) { frame_speed=speed; }
  void set_scroll_speed(int speed) { scroll_speed=speed; }
  void set_pan_speed(int speed) { pan_speed=speed; }
  void set_pan(int xv, int yv, int steps) { pan_xv=xv; pan_yv=yv; pan_steps=steps; }
  void scroll_text(char *Text);
  void wait(void *arg);
  director();
  void set_abort(int x) { scene_abort=x; }
} ;

extern director scene_director;


#endif
