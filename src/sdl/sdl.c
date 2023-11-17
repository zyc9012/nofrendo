/* vim: set tabstop=3 expandtab:
**
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General 
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** sdl.c
**
** $Id: sdl.c,v 1.2 2001/04/27 14:37:11 neil Exp $
**
*/

#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>
#include <noftypes.h>
#include <bitmap.h>
#include <nofconfig.h>
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nes.h>
#include <nes_pal.h>
#include <nesinput.h>
#include <osd.h>

#define  DEFAULT_SAMPLERATE   44100
#define  DEFAULT_BPS          16
#define  DEFAULT_FRAGSIZE     1024

#define  DEFAULT_WIDTH        256
#define  DEFAULT_HEIGHT       NES_VISIBLE_HEIGHT

/*
** Timer
*/

static void (*timer_callback)(void) = NULL;
static int tick_ideal = 0;
static int tick_interval = 0;

Uint32 mySDLTimer(Uint32 i, void* param)
{
   static int tickDiff = 0;
   Uint32 tickLast;

   tickLast = SDL_GetTicks();

   if (timer_callback)
      timer_callback();
   
   tickDiff += tick_interval - tick_ideal + tickLast - SDL_GetTicks();
   
   if (tickDiff >= 10)
   {
      tickDiff -= 10;
      return tick_interval - 10;
   }
   else
   {
      return tick_interval;
   }
}

int osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize)
{
   double ideal = 1000 / frequency;

   /* these arguments are used only for djgpp, which needs to lock code/data */
   UNUSED(counter);
   UNUSED(countersize);
   UNUSED(funcsize);

   tick_ideal = round(ideal);
   tick_interval = round(ideal / 10) * 10;

   SDL_AddTimer(tick_interval, mySDLTimer, NULL);

   timer_callback = func;

   return 0;
}


/*
** Audio
*/
static int sound_bps = DEFAULT_BPS;
static int sound_samplerate = DEFAULT_SAMPLERATE;
static int sound_fragsize = DEFAULT_FRAGSIZE;
static unsigned char *audioBuffer = NULL;
static void (*audio_callback)(void *buffer, int length) = NULL;
static SDL_AudioDeviceID myAudio;

/* this is the callback that SDL calls to obtain more audio data */
static void sdl_audio_player(void *udata, unsigned char *stream, int len)
{
   memset(stream, 0, len);

   /* SDL requests buffer fills in terms of bytes, not samples */
   if (16 == sound_bps)
      len /= 2;

   if (audio_callback)
      audio_callback(stream, len);
}

void osd_setsound(void (*playfunc)(void *buffer, int length))
{
   audio_callback = playfunc;
}

static void osd_stopsound(void)
{
   audio_callback = NULL;

   SDL_CloseAudioDevice(myAudio);
   if (NULL != audioBuffer)
      free(audioBuffer);
}

static int osd_init_sound(void)
{
   SDL_AudioSpec wanted, obtained;
   unsigned int bufferSize;

   sound_bps = config.read_int("sdlaudio", "sound_bps", DEFAULT_BPS);
   sound_samplerate = config.read_int("sdlaudio", "sound_samplerate", DEFAULT_SAMPLERATE);
   sound_fragsize = config.read_int("sdlaudio", "sound_fragsize", DEFAULT_FRAGSIZE);

   if (sound_bps != 8 && sound_bps != 16)
      sound_bps = 8;

   if (sound_samplerate < 5000)
      sound_samplerate = 5000;
   else if (sound_samplerate > 48000)
      sound_samplerate = 48000;

   if (sound_fragsize < 128)
      sound_fragsize = 128;
   else if (sound_fragsize > 32768)
      sound_fragsize = 32768;

   audio_callback = NULL;

   /* set the audio format */
   wanted.freq = sound_samplerate;
   wanted.format = (sound_bps == 8) ? AUDIO_U8 : AUDIO_S16;
   wanted.channels = 1;   /* 1 = mono, 2 = stereo */
   wanted.samples = sound_fragsize;
   wanted.callback = sdl_audio_player;
   wanted.userdata = NULL;

   myAudio = SDL_OpenAudioDevice (NULL, 0, &wanted, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
   if (myAudio == 0)
   {
      log_printf("Couldn't open audio: %s\n", SDL_GetError());
      return -1;
   }

   /* ensure we get U8 or S16 */
   if (AUDIO_U8 != obtained.format && AUDIO_S16 != obtained.format)
   {
      log_printf("Could not get correct audio output format\n");
      return -1;
   }

   sound_bps = (obtained.format == AUDIO_U8) ? 8 : 16;
   sound_samplerate = obtained.freq;
   /* twice as big, to be on the safe side */
   bufferSize = (sound_bps / 8) * obtained.samples * 2;

   audioBuffer = malloc(bufferSize);
   if (NULL == audioBuffer)
   {
      log_printf("error allocating audio buffer\n");
      return -1;
   }

   SDL_PauseAudioDevice(myAudio, 0);
   return 0;
}

void osd_getsoundinfo(sndinfo_t *info)
{
   info->sample_rate = sound_samplerate;
   info->bps = sound_bps;
}

/*
** Video
*/

static int init(int width, int height);
static void shutdown(void);
static int set_mode(int width, int height);
static void set_palette(rgb_t *pal);
static void clear(uint8 color);
static bitmap_t *lock_write(void);
static void free_write(int num_dirties, rect_t *dirty_rects);

viddriver_t sdlDriver =
{
   "Simple DirectMedia Layer",         /* name */
   init,          /* init */
   shutdown,      /* shutdown */
   set_mode,      /* set_mode */
   set_palette,   /* set_palette */
   clear,         /* clear */
   lock_write,    /* lock_write */
   free_write,    /* free_write */
   NULL,          /* custom_blit */
   false          /* invalidate flag */
};

void osd_getvideoinfo(vidinfo_t *info)
{
   info->default_width = DEFAULT_WIDTH;
   info->default_height = DEFAULT_HEIGHT;
   info->driver = &sdlDriver;
}

/* Now that the driver declaration is out of the way, on to the SDL stuff */
static SDL_Window *myWindow = NULL;
static SDL_Surface *mySurface = NULL;
static SDL_Renderer *myRenderer = NULL;
static SDL_Color myPalette[256];
static bitmap_t *myBitmap = NULL;
static bool fullscreen = false;

/* flip between full screen and windowed */
void osd_togglefullscreen(int code)
{
   bool pause;
   nes_t *nes = nes_getcontextptr();

   if (INP_STATE_MAKE != code)
      return;

   ASSERT(nes);

   pause = nes->pause;
   nes->pause = true;

   fullscreen ^= true;

   if (set_mode(mySurface->w, mySurface->h))
      ASSERT(0);

   sdlDriver.invalidate = true;

   nes->pause = pause;
}

/* initialise SDL video */
static int init(int width, int height)
{
   return set_mode(width, height);
}

/* squash memory leaks */
static void shutdown(void)
{
   if (NULL != mySurface)
   {
      SDL_FreeSurface(mySurface);
      mySurface = NULL;
   }

   if (NULL != myBitmap)
      bmp_destroy(&myBitmap);

   if (NULL != myWindow)
   {
      SDL_DestroyWindow(myWindow);
   }
}

/* set a video mode */
static int set_mode(int width, int height)
{
   int flags;
   bool restorePalette;

   if (NULL != myWindow)
   {
      SDL_DestroyWindow(myWindow);
      myWindow = NULL;
      restorePalette = true;
   }

   if (NULL == myWindow)
   {
      flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
      myWindow = SDL_CreateWindow("Nofrendo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width * 2, height * 2, flags);

      if (NULL == myWindow)
      {
         log_printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
         return -1;
      }

      myRenderer = SDL_CreateRenderer(myWindow, -1, SDL_RENDERER_ACCELERATED);

      if (NULL == myRenderer)
      {
         log_printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
         return -1;
      }

      mySurface = SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0);

      if (NULL == mySurface)
      {
         log_printf("SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
         return -1;
      }
   }

   if (fullscreen)
   {
      if (SDL_SetWindowFullscreen(myWindow, SDL_WINDOW_FULLSCREEN) != 0)
      {
         fullscreen = false;
         log_printf("SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
      }
   }

   if (restorePalette)
   {
      SDL_SetPaletteColors(mySurface->format->palette, myPalette, 0, 256);
   }

   SDL_ShowCursor(0);
   return 0;
}

/* copy nes palette over to hardware */
static void set_palette(rgb_t *pal)
{
   int i;

   for (i = 0; i < 256; i++)
   {
      myPalette[i].r = pal[i].r;
      myPalette[i].g = pal[i].g;
      myPalette[i].b = pal[i].b;
   }

   SDL_SetPaletteColors(mySurface->format->palette, myPalette, 0, 256);
}

/* clear all frames to a particular color */
static void clear(uint8 color)
{
   SDL_FillRect(mySurface, 0, color);
}

/* acquire the directbuffer for writing */
static bitmap_t *lock_write(void)
{
   SDL_LockSurface(mySurface);
   myBitmap = bmp_createhw(mySurface->pixels, mySurface->w, 
                           mySurface->h, mySurface->pitch);
   return myBitmap;
}

/* release the resource */
static void free_write(int num_dirties, rect_t *dirty_rects)
{
   bmp_destroy(&myBitmap);
   SDL_UnlockSurface(mySurface);

   SDL_Texture *tex = SDL_CreateTextureFromSurface(myRenderer, mySurface);
   SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
   SDL_RenderCopy(myRenderer, tex, NULL, NULL);
   SDL_RenderPresent(myRenderer);
   SDL_DestroyTexture(tex);
}

/*
** Input
*/

typedef struct joystick_s
{
   SDL_Joystick *js;
   int *button_array;
   int *axis_array;
} joystick_t;

static joystick_t **joystick_array = 0;
static int joystick_count = 0;

static void osd_initinput()
{
   int i, j;
   SDL_Joystick *js;
   char group[255];
   char key[255];

   /* joystick */

   joystick_count = SDL_NumJoysticks();
   joystick_array = malloc(joystick_count * sizeof(joystick_t *));

   if (NULL == joystick_array)
   {
      log_printf("error allocating space for joystick array\n");
      joystick_count = 0;
   }
 
   log_printf("joystick_count == %i\n", joystick_count);

   for (i = 0; i < joystick_count; i++)
   {
      sprintf(group, "sdljoystick%i", i);
      js = SDL_JoystickOpen(i);

      if (js)
      {
         joystick_array[i] = malloc(sizeof(joystick_t));
         joystick_array[i]->js = js;
 
         log_printf("joystick %i is a %s\n", i, SDL_JoystickName(js));

         /* load buttons */
         j = SDL_JoystickNumButtons(joystick_array[i]->js);
         joystick_array[i]->button_array = malloc(j * sizeof(int));
         for (j--; j >= 0; j--)
         {
            sprintf(key, "button%i", j);
            joystick_array[i]->button_array[j] = config.read_int(group, key, event_none);
         }

         /* load axes */
         j = SDL_JoystickNumAxes(joystick_array[i]->js);
         joystick_array[i]->axis_array = malloc(j * sizeof(int) * 2);
         for (j--; j >= 0; j--)
         {
            sprintf(key, "positiveaxis%i", j);
            joystick_array[i]->axis_array[(j << 1) + 1] = config.read_int(group, key, event_none);

            sprintf(key, "negativeaxis%i", j);
            joystick_array[i]->axis_array[j << 1] = config.read_int(group, key, event_none);
         }
      }
      else
      {
         joystick_array[i] = 0;
      }
   }

   SDL_JoystickEventState(SDL_ENABLE);

   /* events */

   event_set(event_osd_1, osd_togglefullscreen);
}

static int key_to_event(SDL_KeyCode key)
{
   switch (key)
   {
      case SDLK_ESCAPE: return event_quit;
      
      case SDLK_F1: return event_soft_reset;
      case SDLK_F2: return event_hard_reset;
      case SDLK_F3: return event_gui_toggle_fps;
      case SDLK_F4: return event_snapshot;
      case SDLK_F5: return event_state_save;
      case SDLK_F6: return event_toggle_sprites;
      case SDLK_F7: return event_state_load;
      case SDLK_F10: return event_osd_1;

      case SDLK_1: return event_state_slot_1;
      case SDLK_EXCLAIM: return event_state_slot_1;
      case SDLK_2: return event_state_slot_2;
      case SDLK_AT: return event_state_slot_2;
      case SDLK_3: return event_state_slot_3;
      case SDLK_HASH: return event_state_slot_3;
      case SDLK_4: return event_state_slot_4;
      case SDLK_DOLLAR: return event_state_slot_4;
      case SDLK_5: return event_state_slot_5;
   /*   case SDLK_PERCENT: return event_state_slot_5;*/
      case SDLK_6: return event_state_slot_6;
      case SDLK_CARET: return event_state_slot_6;
      case SDLK_7: return event_state_slot_7;
      case SDLK_AMPERSAND: return event_state_slot_7;
      case SDLK_8: return event_state_slot_8;
      case SDLK_ASTERISK: return event_state_slot_8;
      case SDLK_9: return event_state_slot_9;
      case SDLK_LEFTPAREN: return event_state_slot_9;
      case SDLK_0: return event_state_slot_0;
      case SDLK_RIGHTPAREN: return event_state_slot_0;

      case SDLK_MINUS: return event_gui_pattern_color_down;
      case SDLK_UNDERSCORE: return event_gui_pattern_color_down;
      case SDLK_EQUALS: return event_gui_pattern_color_up;
      case SDLK_PLUS: return event_gui_pattern_color_up;

      case SDLK_BACKSPACE: return event_gui_display_info;

      case SDLK_TAB: return event_joypad1_select;

      case SDLK_q: return event_toggle_channel_0;
      case SDLK_w: return event_toggle_channel_1;
      case SDLK_e: return event_toggle_channel_2;
      case SDLK_r: return event_toggle_channel_3;
      case SDLK_t: return event_toggle_channel_4;
      case SDLK_y: return event_toggle_channel_5;
      case SDLK_u: return event_palette_hue_down;
      case SDLK_i: return event_palette_hue_up;
      case SDLK_o: return event_gui_toggle_oam;
      case SDLK_p: return event_gui_toggle_pattern;

      case SDLK_BACKSLASH: return event_toggle_frameskip;
   /*   case SDLK_Bar: return event_toggle_frameskip;*/

      case SDLK_a: return event_gui_toggle_wave;
      case SDLK_s: return event_set_filter_0;
      case SDLK_d: return event_set_filter_1;
      case SDLK_f: return event_set_filter_2;
      case SDLK_j: return event_palette_tint_down;
      case SDLK_k: return event_palette_tint_up;
      case SDLK_l: return event_palette_set_shady;
      case SDLK_COLON: return event_palette_set_default;
      case SDLK_SEMICOLON: return event_palette_set_default;
      case SDLK_RETURN: return event_joypad1_start;
      case SDLK_PAUSE: return event_togglepause;

      case SDLK_z: return event_joypad1_b;
      case SDLK_x: return event_joypad1_a;
      case SDLK_c: return event_joypad1_select;
      case SDLK_v: return event_joypad1_start;
      case SDLK_b: return event_joypad2_b;
      case SDLK_n: return event_joypad2_a;
      case SDLK_m: return event_joypad2_select;
      case SDLK_COMMA: return event_joypad2_start;

      case SDLK_SPACE: return event_gui_toggle;

      case SDLK_LCTRL: return event_joypad1_b;
      case SDLK_RCTRL: return event_joypad1_b;
      case SDLK_LALT: return event_joypad1_a;
      case SDLK_RALT: return event_joypad1_a;
      case SDLK_LSHIFT: return event_joypad1_a;
      case SDLK_RSHIFT: return event_joypad1_a;

      case SDLK_KP_2: return event_joypad1_down;
      case SDLK_KP_4: return event_joypad1_left;
      case SDLK_KP_5: return event_startsong;
      case SDLK_KP_6: return event_joypad1_right;
      case SDLK_KP_7: return event_songdown;
      case SDLK_KP_8: return event_joypad1_up;
      case SDLK_KP_9: return event_songup;
      case SDLK_UP: return event_joypad1_up;
      case SDLK_DOWN: return event_joypad1_down;
      case SDLK_LEFT: return event_joypad1_left;
      case SDLK_RIGHT: return event_joypad1_right;
      case SDLK_KP_PLUS: return event_toggle_frameskip;
   }
   return event_none;
}

void osd_getinput(void)
{
   int code, highval, lowval;
   SDL_Event myEvent;
   event_t func_event;

   while (SDL_PollEvent(&myEvent))
   {
      switch(myEvent.type)
      {
      case SDL_KEYDOWN:
      case SDL_KEYUP:
         code = (myEvent.key.state == SDL_PRESSED) ? INP_STATE_MAKE : INP_STATE_BREAK;

         func_event = event_get(key_to_event(myEvent.key.keysym.sym));
         if (func_event)
            func_event(code);
         break;

      case SDL_QUIT:
         event_get(event_quit)(INP_STATE_MAKE);
         break;

      case SDL_JOYAXISMOTION:
         highval = (myEvent.jaxis.value > 0) ? INP_STATE_MAKE : INP_STATE_BREAK;
         lowval  = (myEvent.jaxis.value < 0) ? INP_STATE_MAKE : INP_STATE_BREAK;

         func_event = event_get(joystick_array[myEvent.jaxis.which]->axis_array[myEvent.jaxis.axis << 1]);
         if (func_event)
            func_event(lowval);

         func_event = event_get(joystick_array[myEvent.jaxis.which]->axis_array[(myEvent.jaxis.axis << 1) + 1]);
         if (func_event)
            func_event(highval);
         break;

      case SDL_JOYBUTTONDOWN:
      case SDL_JOYBUTTONUP:
         code = (myEvent.jbutton.state == SDL_PRESSED) ? INP_STATE_MAKE : INP_STATE_BREAK;

         func_event = event_get( joystick_array[myEvent.jbutton.which]->button_array[myEvent.jbutton.button] );
         if (func_event)
            func_event(code);
         break;

      default:
         break;
      }
   }
}

static void osd_freeinput(void)
{
   int i;

   for (i = 0; i < joystick_count; i++)
   {
      if (joystick_array[i])
      {
         SDL_JoystickClose(joystick_array[i]->js);
         free(joystick_array[i]->button_array);
         free(joystick_array[i]->axis_array);
         free(joystick_array[i]);
      }
   }

   free(joystick_array);
}

void osd_getmouse(int *x, int *y, int *button)
{
   *button = SDL_GetMouseState(x, y);
}

uint32 osd_get_ticks()
{
   return SDL_GetTicks();
}

void osd_delay(uint32 ms)
{
   SDL_Delay(ms);
}

/*
** Shutdown
*/

/* this is at the bottom, to eliminate warnings */
void osd_shutdown()
{
   osd_stopsound();
   osd_freeinput();
   SDL_Quit();
}

static int logprint(const char *string)
{
   return fprintf(stderr, "%s", string);
}

/*
** Startup
*/

int osd_init()
{
   log_chain_logfunc(logprint);

   /* Initialize the SDL library */
   if (SDL_Init (SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_VIDEO
                 | SDL_INIT_JOYSTICK) < 0)
   {
      printf("Couldn't initialize SDL: %s\n", SDL_GetError());
      return -1;
   }

   if (osd_init_sound())
      return -1;

   osd_initinput();

   return 0;
}

/*
** $Log: sdl.c,v $
** Revision 1.2  2001/04/27 14:37:11  neil
** wheeee
**
** Revision 1.1.1.1  2001/04/27 07:03:54  neil
** initial
**
** Revision 1.16  2000/12/11 13:31:41  neil
** caption
**
** Revision 1.15  2000/11/25 20:29:42  matt
** tiny mod
**
** Revision 1.14  2000/11/09 14:06:31  matt
** state load fixed, state save mostly fixed
**
** Revision 1.13  2000/11/06 02:20:00  matt
** vid_drv / log api changes
**
** Revision 1.12  2000/11/05 16:36:06  matt
** thinlib round 2
**
** Revision 1.11  2000/11/05 06:26:41  matt
** thinlib spawns changes
**
** Revision 1.10  2000/11/01 17:31:54  neil
** fixed some conflicting key assignments
**
** Revision 1.9  2000/11/01 14:17:16  matt
** multi-system event system, or whatever
**
** Revision 1.8  2000/10/23 15:54:15  matt
** suppressed warnings
**
** Revision 1.7  2000/10/22 20:37:33  neil
** restored proper timer correction, and added support for variable frequencies
**
** Revision 1.6  2000/10/22 19:17:06  matt
** more sane timer ISR / autoframeskip
**
** Revision 1.5  2000/10/21 19:34:03  matt
** many more cleanups
**
** Revision 1.4  2000/10/17 11:59:29  matt
** let me see why i can't go window->full->window
**
** Revision 1.3  2000/10/13 14:09:57  matt
** sound is configurable from config file
**
** Revision 1.2  2000/10/13 13:19:15  matt
** fixed a few minor bugs and 16-bit sound
**
** Revision 1.1  2000/10/10 14:24:25  matt
** initial revision
**
*/
