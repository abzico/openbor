#include "devconsole.h"
#include "openbor.h"
#include "hankaku.h"
#include "version.h"

#define HISTORY_LINES 1000

/* from openbor.c */
extern bool _devconsole;                      // get/set whether dev console is shown
extern s_screen*  vscreen;
extern s_videomodes videomodes;
extern int color_white;
extern int SAMPLE_PAUSE;                      // use pause sample sfx when dev console activated

/* from openbor.c/.h */
extern bool is_dev_console_enabled;       // is_dev_console_enabled

/* from control.h */
extern bool is_dev_console_triggered;         // whether key combination for dev console triggered  
extern bool is_dev_console_shouldbe_visible;  // flag whether dev console shouldbe visible
extern char dev_console_inputstr[DEVCONSOLE_MAX_CHAR_INPUT];  // input string from prompt

/* global variables */
bool is_dev_console_triggered = false;
bool is_dev_console_shouldbe_visible = false;

/* internal variables */
#define LINE_BUFFER 512                       // each line can contain at max number of characters
#define MAX_LINES 100                         // keep for number of lines at most
                                              // pls keep it low as current algo work in good perf with
                                              // low number (it expects it)
#define HISTLINE_DUMMY 99999
static Uint32 histline_last = HISTLINE_DUMMY; // keep track of last line of histline (index-based)
static char* histlines[MAX_LINES];                   // dynamic cyclic-array allocated string of histlines
static int color_gray;                        // color for histlines

void devconsole_init()
{
  for (int i=0; i<MAX_LINES; ++i)
  {
    histlines[i] = NULL;
  }

  color_gray = _makecolour(200, 200, 200);
}

void devconsole_shutdown()
{
  // free string if needed
  for (int i=0; i<MAX_LINES; ++i)
  {
    if (histlines[i] != NULL)
    {
      free(histlines[i]);
      histlines[i] = NULL;
    }
  }
}

void devconsole_reset()
{
  histline_last = HISTLINE_DUMMY;
  is_dev_console_triggered = false;
  is_dev_console_shouldbe_visible = false;

  // free string if needed
  for (int i=0; i<MAX_LINES; ++i)
  {
    if (histlines[i] != NULL)
    {
      free(histlines[i]);
      histlines[i] = NULL;
    }
  }
}

/** TODO: refactor and move this to common lib function to used in multiple place, along with modify
 * to support more generic s_screen.
 *
 * Another place that it's used is at menu.c
 */
static void printText(s_screen* screen, int bpp, int x, int y, int col, int backcol, int fill, char *format, ...)
{
	int x1, y1, i;
	u32 data;
	u16 *line16 = NULL;
	u32 *line32 = NULL;
	u8 *font;
	u8 ch = 0;
	char buf[LINE_BUFFER] = {""};
	int pitch = screen->width*bpp/8;
	va_list arglist;
	va_start(arglist, format);
	vsprintf(buf, format, arglist);
	va_end(arglist);

	for(i=0; i<sizeof(buf); i++)
	{
		ch = buf[i];
		// mapping
		if (ch<0x20) ch = 0;
		else if (ch<0x80) { ch -= 0x20; }
		else if (ch<0xa0) {	ch = 0;	}
		else ch -= 0x40;
		font = (u8 *)&hankaku_font10[ch*10];
		// draw
		if (bpp == 16) line16 = (u16*)(screen->data + x*2 + y * pitch);
		else           line32 = (u32*)(screen->data + x*4 + y * pitch);

		for (y1=0; y1<10; y1++)
		{
			data = *font++;
			for (x1=0; x1<5; x1++)
			{
				if (data & 1)
				{
					if (bpp == 16) *line16 = col;
					else           *line32 = col;
				}
				else if (fill)
				{
					if (bpp == 16) *line16 = backcol;
					else           *line32 = backcol;
				}

				if (bpp == 16) line16++;
				else           line32++;

				data = data >> 1;
			}
			if (bpp == 16) line16 += pitch/2-5;
			else           line32 += pitch/4-5;
		}
		x+=5;
	}
}

/**
 * add line into history line thus showing on devconsole screen 
 *
 * impl note: implement as cyclic-array (might need perf/algo cleanup and improvement),
 * computed `index` is next-to-be-used element to dynamically allocate for string of next
 * line to be copied.
 *
 * It truncate a line with LINE_BUFFER, then update next last to-be-used element index for
 * next round.
 *
 * Use this to add a single line into histline. If you have multiple lines to add, call this
 * function multiple times.
 *
 * \param line a null-terminated string.
 */
static void add_to_histline(const char* line)
{
  if (line == NULL)
    return;
  
  int len = strlen(line);
  if (len == 0)
    return;

  // free previous allocated string
  int index = histline_last == HISTLINE_DUMMY ? MAX_LINES-1 : histline_last-1;
  if (index < 0)
    index = MAX_LINES -1;
  if (histlines[index] != NULL)
  {
    free(histlines[index]);
    histlines[index] = NULL;
  }

  // truncate string for length of our max buffer (if need)
  if (len > LINE_BUFFER-1)    // not include null-terminated character
  {
    histlines[index] = malloc(sizeof(char) * LINE_BUFFER);
    
    strncpy(histlines[index], line, LINE_BUFFER-1);
    histlines[index][LINE_BUFFER-1] = '\0';
  }
  // copy string
  else
  {
    histlines[index] = malloc(sizeof(char) * (len+1));
    strncpy(histlines[index], line, len+1);
#ifdef DEBUG
    fprintf(stdout, "histlines[index] = %s\n", histlines[index]);
#endif
  }

  // update last element index
  histline_last = histline_last == HISTLINE_DUMMY ? MAX_LINES : histline_last;
  if (histline_last == 0)
  {
    histline_last = MAX_LINES-1;
  }
  else
  {
    histline_last = (histline_last - 1) % MAX_LINES;
  }
#ifdef DEBUG
  fprintf(stdout, "histline_last = %d\n", histline_last);
#endif
}

/**
 * Render all histlines on devconsole
 *
 * impl note: it puts more priority in rendering recent lines first before older lines which
 * might be outside of devconsole area for users to see.
 *
 * Anyway we still keep those older lines; for future in case we implement a scrolling up/down.
 *
 * \param screen screen to render histlines onto
 */
static void render_all_histlines(s_screen* screen, int x, int start_y, int color, int char_height)
{
  if (histline_last == HISTLINE_DUMMY)
    return;

  for (int i=0; i<MAX_LINES; ++i)
  {
    int k = (histline_last+i) % MAX_LINES;

    if (histlines[k] == NULL)
      break;

    printText(screen, 32, x, start_y - (i+1)*char_height, color, 0, 0, histlines[k]);
  }
}

bool devconsole_control_update(Uint8 *keystate, const SDL_Event event)
{
  if (event.type == SDL_KEYDOWN)
  {
    // trigger to show dev's console
    // hard-coded with combination of keys left-ctrl + left-alt + c
    // TODO: might introduce compilation flag for release build so we
    // could ignore this block of code completely
    if (!_devconsole &&
        is_dev_console_enabled &&
        !is_dev_console_triggered &&
        !is_dev_console_shouldbe_visible &&
        keystate[SDL_SCANCODE_LCTRL] &&
        keystate[SDL_SCANCODE_LALT] &&
        keystate[SDL_SCANCODE_C])
    {
#ifdef DEBUG
      fprintf(stdout, "trigger dev's console\n");
#endif
      is_dev_console_triggered = true;
      is_dev_console_shouldbe_visible = true;
      return true;
    }
    else if (_devconsole &&
        is_dev_console_enabled &&
        !is_dev_console_triggered &&
        is_dev_console_shouldbe_visible &&
        keystate[SDL_SCANCODE_LCTRL] &&
        keystate[SDL_SCANCODE_LALT] &&
        keystate[SDL_SCANCODE_C])
    {
#ifdef DEBUG
      fprintf(stdout, "trigger dev's console (to hide)\n");
#endif
      is_dev_console_triggered = true;
      is_dev_console_shouldbe_visible = false;
      return true;
    }
    else if (_devconsole &&
        is_dev_console_enabled &&
        is_dev_console_shouldbe_visible &&
        keystate[SDL_SCANCODE_BACKSPACE])
    {
        // backspace while devconsole is shown
        // remove the last character from input string
        int len = strlen(dev_console_inputstr);
        if (len > 0)
        {
          dev_console_inputstr[len-1] = '\0';
        }
        return true;
    }
    else if (_devconsole &&
        is_dev_console_enabled &&
        is_dev_console_shouldbe_visible)
    {
      // consume keys not to propagate to next one
      // this we check for enter key then to perform the command
			
      int lastkey = event.key.keysym.scancode;
      if(lastkey == SDL_SCANCODE_RETURN)
      {
#ifdef DEBUG
        fprintf(stdout, "enter an issued command\n");
#endif
        keystate[SDL_SCANCODE_RETURN] = 0;

        // perform command
        //perform_command(dev_console_inputstr
        ArgList arglist;
        char argbuf[MAX_ARG_LEN + 1] = "";

        if (ParseArgs(&arglist, dev_console_inputstr, argbuf))
        {
          char* command = GET_ARG(0);
          if(command && command[0])
          {
            // TODO: add more commands to support here
            if(stricmp(command, "version") == 0)
            {
              char buf[LINE_BUFFER];
              snprintf(buf, LINE_BUFFER, "OpenBOR v.%s.%s Build %d (%s)", VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD_INT, VERSION_COMMIT);

              add_to_histline(buf);
            }
          }
        }
        
        // clear current input string
        memset(dev_console_inputstr, 0, sizeof(dev_console_inputstr));
        return true;
      }
    }
  }
  else if (event.type == SDL_TEXTINPUT)
  {
    // only process when devconsole is already shown
    if (_devconsole &&
        is_dev_console_enabled &&
        is_dev_console_shouldbe_visible)
    {
      strcat(dev_console_inputstr, event.text.text);
      return true;
    }
  }
  else if (event.type == SDL_TEXTEDITING)
  {
    // only process when devconsole is already shown
    //if (_devconsole &&
    //    is_dev_console_enabled &&
    //    is_dev_console_shouldbe_visible)
    //{
    //  
    //}
  }

  // all else, not consume
  return false;
}

/**
 * frame update function for dev's console feature.
 */
void devconsole_perframe_update()
{
	int quit = 0;
	s_screen *gamebuffer = allocscreen(videomodes.hRes, videomodes.vRes, PIXEL_32);

  int color_black = _makecolour(0, 0, 0);

	// copy current game screen into our screen buffer
	copyscreen(gamebuffer, vscreen);
	spriteq_draw(gamebuffer, 0, MIN_INT, MAX_INT, 0, 0);
  spriteq_clear();
	spriteq_add_screen(0, 0, LAYER_Z_LIMIT_BOX_MAX, gamebuffer, NULL, 0);
	spriteq_add_box(0, 0, videomodes.hRes, videomodes.vRes / 2, LAYER_Z_LIMIT_BOX_MAX, color_black, NULL);
	spriteq_add_line(0, videomodes.vRes / 2, videomodes.hRes, videomodes.vRes / 2, LAYER_Z_LIMIT_BOX_MAX, color_white, NULL);

	spriteq_lock();

  const int kchar_height = 10;
  const int kchar_width = 5;
  int start_y = videomodes.vRes / 2 - kchar_height;
  int left_margin = 3;

  bool cursor_on = true;
#define CURSOR_FLASH_COUNTER_MAX 20             // adjust this to make flashing faster or slower
  int cursor_flash_counter = 0;
  int cursor_x = left_margin + kchar_width + 3;
  int cursor_offset_x = 0;

  // start receiving text input event from SDL2
  // key control for devconsole is implemented in control.c
  SDL_StartTextInput();

	while(!quit)
	{
    // base sprite
		spriteq_draw(gamebuffer, 0, MIN_INT, MAX_INT, 0, 0);

    // prompt symbol
		printText(gamebuffer, 32, left_margin, start_y, color_white, 0, 0, "$");

    // calculate the offset x of input string as detected in control.c
    cursor_offset_x = strlen(dev_console_inputstr) * 5;

    // cursor 
    putbox(cursor_x + cursor_offset_x, start_y, kchar_width, kchar_height - 1, cursor_on ? color_white : color_black, gamebuffer, NULL);
    // text input
    printText(gamebuffer, 32, cursor_x, start_y, color_white, 0, 0, dev_console_inputstr);

    // histlines
    render_all_histlines(gamebuffer, cursor_x, start_y, color_gray, kchar_height);

    // blit finally using the current stretch option in videomodes
	  video_copy_screen(vscreen);

		// if console is triggered (to hide) then hide it
		if (_devconsole &&
				is_dev_console_triggered &&
				!is_dev_console_shouldbe_visible)
		{
#ifdef DEBUG
			fprintf(stdout, "hide dev's console\n");
#endif

			is_dev_console_triggered = false;
			is_dev_console_shouldbe_visible = false;
			_devconsole = false;

			sound_pause_music(0);
			sound_pause_sample(0);
			sound_play_sample(SAMPLE_PAUSE, 0, savedata.effectvol, savedata.effectvol, 100);
			// now quit
			quit = 1;
		}
    
    if (_devconsole)
    {
      update(1, 0);
    }

    cursor_flash_counter++;
    if (cursor_flash_counter >= CURSOR_FLASH_COUNTER_MAX)
    {
      cursor_flash_counter = 0;
      cursor_on = !cursor_on;
    }

    // note: added this to fix crash problem when switching back and forth between
    // full screen and windowed mode rapidly
    SDL_Delay(1);
	}

  // end receiving text input from SDL2
  SDL_StopTextInput();

	spriteq_unlock();
	spriteq_clear();

	freescreen(&gamebuffer);
}
