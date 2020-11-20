/*
 * Multicomp MP730424 Multimeter, 55k count, serial only
 *
 * V0.1 - July 20, still COVID-19 ravages the earth
 * 
 *
 * Written by Paul L Daniels (pldaniels@gmail.com)
 *
 */

#include <SDL.h>
#include <SDL_ttf.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define FL __FILE__,__LINE__

/*
 * Should be defined in the Makefile to pass to the compiler from
 * the github build revision
 *
 */
#ifndef BUILD_VER 
#define BUILD_VER 000
#endif

#ifndef BUILD_DATE
#define BUILD_DATE " "
#endif

#define SSIZE 1024

#define INTERFRAME_SLEEP	200000 // 0.2 seconds

#define DATA_FRAME_SIZE 12
#define ee ""
#define uu "\u00B5"
#define kk "k"
#define MM "M"
#define mm "m"
#define nn "n"
#define pp "p"
#define dd "\u00B0"
#define oo "\u03A9"

#define MMFLAG_AUTORANGE	0b01000000

#define CMODE_USB 1
#define CMODE_SERIAL 2
#define CMODE_NONE 0

//#define MEAS_VOLT "MEAS:VOLT?"
//#define MEAS_CURR "MEAS:CURR?"
#define GET_FUNC "FUNC?"
#define MEAS_VALUE "MEAS?"

char SEPARATOR_DP[] = ".";

struct serial_params_s {
	char *device;
	int fd, n;
	int cnt, size, s_cnt;
	struct termios oldtp, newtp;
};


struct glb {
	uint8_t debug;
	uint8_t quiet;
	uint16_t flags;
	uint16_t error_flag;
	char *output_file;
	char *device;

	char get_func[20];
	char meas_value[20];

	int usb_fhandle;

	int comms_mode;
	char *com_address;
	char *serial_parameters_string; // this is the raw from the command line
	struct serial_params_s serial_params; // this is the decoded version
	int serial_timeout;


	int interval;
	int font_size;
	int window_width, window_height;
	int wx_forced, wy_forced;
	SDL_Color font_color_volts, font_color_amps, background_color;
};

/*
 * A whole bunch of globals, because I need
 * them accessible in the Windows handler
 *
 * So many of these I'd like to try get away from being
 * a global.
 *
 */
struct glb *glbs;

/*
 * Test to see if a file exists
 *
 * Does not test anything else such as read/write status
 *
 */
bool fileExists(const char *filename) {
	struct stat buf;
	return (stat(filename, &buf) == 0);
}


char digit( unsigned char dg ) {

	int d;
	char g;

	switch (dg) {
		case 0x30: g = '0'; d = 0; break;
		case 0x31: g = '1'; d = 1; break;
		case 0x32: g = '2'; d = 2; break;
		case 0x33: g = '3'; d = 3; break;
		case 0x34: g = '4'; d = 4; break;
		case 0x35: g = '5'; d = 5; break;
		case 0x36: g = '6'; d = 6; break;
		case 0x37: g = '7'; d = 7; break;
		case 0x38: g = '8'; d = 8; break;
		case 0x39: g = '9'; d = 9; break;
		case 0x3E: g = 'L'; d = 0; break;
		case 0x3F: g = ' '; d = 0; break;
		default: g = ' ';
	}

	return g;
}

/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220248
  Function Name	: init
  Returns Type	: int
  ----Parameter List
  1. struct glb *g ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int init(struct glb *g) {
	g->debug = 0;
	g->quiet = 0;
	g->flags = 0;
	g->error_flag = 0;
	g->output_file = NULL;
	g->interval = 100000;
	g->device = NULL;
	g->comms_mode = CMODE_NONE;
	g->serial_timeout = 3;

	g->serial_parameters_string = NULL;

	g->font_size = 60;
	g->window_width = 400;
	g->window_height = 100;
	g->wx_forced = 0;
	g->wy_forced = 0;

	g->font_color_volts =  { 10, 200, 10 };
	g->font_color_amps =  { 200, 200, 10 };
	g->background_color = { 0, 0, 0 };

	return 0;
}

void show_help(void) {
	fprintf(stdout,"MP730424 Multimeter OSD\r\n"
			"By Paul L Daniels / pldaniels@gmail.com\r\n"
			"Build %d / %s\r\n"
			"\r\n"
			"\t-h: This help\r\n"
			"\t-d: debug enabled\r\n"
			"\t-q: quiet output\r\n"
			"\t-v: show version\r\n"
			"\t-z <font size in pt>\r\n"
			"\t-cv <volts colour, a0a0ff>\r\n"
			"\t-ca <amps colour, ffffa0>\r\n"
			"\t-cb <background colour, 101010>\r\n"
			"\t-t <interval>, sleep delay between samples, default 100,000us)\r\n"
			"\t-p <comport>, Set the com port for the meter, eg: -p /dev/ttyUSB0\r\n"
			"\t-s <[115200|57600|38400|9600|4800|2400]:8[o|e|n][1|2]>, eg: -s 57600:8n1\r\n"
			"\t--timeout <seconds>, time to wait for serial response, default 3 seconds\r\n"
			"\r\n"
			"\texample: mp730424 -s 115200:8n1\r\n"
			, BUILD_VER
			, BUILD_DATE 
			);
} 


/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220258
  Function Name	: parse_parameters
  Returns Type	: int
  ----Parameter List
  1. struct glb *g,
  2.  int argc,
  3.  char **argv ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int parse_parameters(struct glb *g, int argc, char **argv ) {
	int i;

	if (argc == 1) {
		show_help();
		exit(1);
	}

	for (i = 0; i < argc; i++) {
		if (argv[i][0] == '-') {
			/* parameter */
			switch (argv[i][1]) {

				case '-':
					if (strncmp(argv[i], "--timeout", 9)==0) {
						i++;
						g->serial_timeout = strtol(argv[i], NULL, 10);
					}
					break;

				case 'h':
					show_help();
					exit(1);
					break;

				case 'z':
					i++;
					if (i < argc) {
						g->font_size = atoi(argv[i]);
					} else {
						fprintf(stdout,"Insufficient parameters; -z <font size pts>\n");
						exit(1);
					}
					break;

				case 'p':
					/*
					 * com port can be multiple things in linux
					 * such as /dev/ttySx or /dev/ttyUSBxx
					 */
					i++;
					if (i < argc) {
						g->device = argv[i];
					} else {
						fprintf(stdout,"Insufficient parameters; -p <usb TMC port ie, /dev/usbtmc2>\n");
						exit(1);
					}
					break;

				case 'o':
					/* 
					 * output file where this program will put the text
					 * line containing the information FlexBV will want 
					 *
					 */
					i++;
					if (i < argc) {
						g->output_file = argv[i];
					} else {
						fprintf(stdout,"Insufficient parameters; -o <output file>\n");
						exit(1);
					}
					break;

				case 'd': g->debug = 1; break;

				case 'q': g->quiet = 1; break;

				case 'v':
							 fprintf(stdout,"Build %d\r\n", BUILD_VER);
							 exit(0);
							 break;

				case 't':
							 i++;
							 g->interval = atoi(argv[i]);
							 break;

				case 'c':
							 int a,b,c;
							 if (argv[i][2] == 'v') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &g->font_color_volts.r
										 , &g->font_color_volts.g
										 , &g->font_color_volts.b
										 );

							 } else if (argv[i][2] == 'a') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &g->font_color_amps.r
										 , &g->font_color_amps.g
										 , &g->font_color_amps.b
										 );

							 } else if (argv[i][2] == 'b') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &(g->background_color.r)
										 , &(g->background_color.g)
										 , &(g->background_color.b)
										 );

							 }
							 break;

				case 'w':
							 if (argv[i][2] == 'x') {
								 i++;
								 g->wx_forced = atoi(argv[i]);
							 } else if (argv[i][2] == 'y') {
								 i++;
								 g->wy_forced = atoi(argv[i]);
							 }
							 break;

				case 's':
							 i++;
							 g->serial_parameters_string = argv[i];
							 break;

				default: break;
			} // switch
		}
	}

	return 0;
}



/*
 * Default parameters are 2400:8n1, given that the multimeter
 * is shipped like this and cannot be changed then we shouldn't
 * have to worry about needing to make changes, but we'll probably
 * add that for future changes.
 *
 */
void open_port( struct glb *g ) {
#ifdef __linux__
	struct serial_params_s *s = &(g->serial_params);
	char *p = g->serial_parameters_string;
	char default_params[] = "115200:8n1";
	int r; 

	if (!p) p = default_params;

	fprintf(stdout,"Attempting to open '%s'\n", s->device);
	s->fd = open( s->device, O_RDWR | O_NOCTTY | O_NDELAY );
	if (s->fd <0) {
		perror( s->device );
	}

	fcntl(s->fd,F_SETFL,0);
	tcgetattr(s->fd,&(s->oldtp)); // save current serial port settings 
	tcgetattr(s->fd,&(s->newtp)); // save current serial port settings in to what will be our new settings
	cfmakeraw(&(s->newtp));

	s->newtp.c_cflag = CS8 |  CLOCAL | CREAD ; 

	if (strncmp(p, "115200:", 7) == 0) s->newtp.c_cflag |= B115200; 
	else if (strncmp(p, "57600:", 6) == 0) s->newtp.c_cflag |= B57600;
	else if (strncmp(p, "38400:", 6) == 0) s->newtp.c_cflag |= B38400;
	else if (strncmp(p, "19200:", 6) == 0) s->newtp.c_cflag |= B19200;
	else if (strncmp(p, "9600:", 5) == 0) s->newtp.c_cflag |= B9600;
	else if (strncmp(p, "4800:", 5) == 0) s->newtp.c_cflag |= B4800;
	else if (strncmp(p, "2400:", 5) == 0) s->newtp.c_cflag |= B2400; //
	else {
		fprintf(stdout,"Invalid serial speed\r\n");
		exit(1);
	}


	s->newtp.c_cc[VMIN] = 0;
	s->newtp.c_cc[VTIME] = g->serial_timeout *10; // VTIME is 1/10th's of second

	p = strchr(p,':');
	if (p) {
		p++;
		switch (*p) {
			case '8': break;
			default: 
						 fprintf(stdout, "Meter only accepts 8 bit mode\n");
		}

		p++;
		switch (*p) {
			case 'o': 
				s->newtp.c_cflag |= PARODD;
				break;
			case 'n': 
				s->newtp.c_cflag &= ~(PARODD|PARENB);
				break;
			case 'e': 
				s->newtp.c_cflag |= PARENB;
				break;
			default: 
				fprintf(stdout, "Parity mode is [n]one, [o]dd, or [e]ven\n");
		}

		p++;
		switch (*p) {
			case '1': 
				s->newtp.c_cflag &= ~CSTOPB;
				break;
			case '2': 
				s->newtp.c_cflag |= CSTOPB;
				break;
			default: 
				fprintf(stdout, "Stop bits are 1, or 2 only\n");
		}

	}

	s->newtp.c_iflag &= ~(IXON | IXOFF | IXANY );

	r = tcsetattr(s->fd, TCSANOW, &(s->newtp));
	if (r) {
		fprintf(stderr,"%s:%d: Error setting terminal (%s)\n", FL, strerror(errno));
		exit(1);
	}

	fprintf(stdout,"Serial port opened, FD[%d]\n", s->fd);
#endif
}

uint8_t a2h( uint8_t a ) {
	a -= 0x30;
	if (a < 10) return a;
	a -= 7;
	return a;
}

ssize_t data_read( glb *g, char *b, ssize_t s ) {
	ssize_t sz;
	*b = '\0';
	if (g->comms_mode == CMODE_USB) {
		/*
		 * usb mode read
		 *
		 */
		int bp = 0;
		do {
			sz = read(g->usb_fhandle, b+bp, s -1 -bp);
			b[bp+sz] = '\0';

			if (sz == -1) {
				g->error_flag = true;
				fprintf(stdout,"Error reading data: %s\n", strerror(errno));
				snprintf(b, s, "NODATA");
				break;
			}

			bp += sz;
			if (sz == 0) break;
			if (bp >= s) break;
			usleep(1000);
		} while (sz);
		b[bp] = '\0';
		if ((bp > 0) && b[bp-1] == '\n') b[bp -1] = '\0';

	} else {
		/*
		 * serial mode read
		 *
		 */
		int bp = 0;

		do {
			char temp_char;
			sz = read(g->serial_params.fd, &temp_char, 1);
			if (sz == -1) {
				b[0] = '\0';
				fprintf(stderr,"Cannot read data from serial port - %s\n", strerror(errno));
				return sz;
			}
			if (sz > 0) {
				b[bp] = temp_char;
				if (b[bp] == '\n') break;
				bp++;
			} else b[bp] = '\0';
		} while (sz > 0 && bp < s);
		b[bp] = '\0';

	}

	return sz;
}

ssize_t data_write( glb *g, char *d, ssize_t s ) { 
	ssize_t sz;

	if (g->comms_mode == CMODE_USB) {
		/*
		 * usb mode write
		 *
		 */
		sz = write(g->usb_fhandle, d, s); //"MEAS:VOLT?", sizeof("MEAS:VOLT?"));
		if (sz < 0) {
			g->error_flag = true;
			fprintf(stdout,"Error sending USB data: %s\n", strerror(errno));
			return -1;
		}
	} else {
		/*
		 * serial mode write
		 *
		 */
		sz = write(g->serial_params.fd, d, s); 
		if (sz < 0) {
			g->error_flag = true;
			fprintf(stdout,"Error sending serial data: %s\n", strerror(errno));
		}
	}

	return sz;
}

#ifdef __WIN32
void parse_serial_parameters( struct glb *g ) {
	char *p = g->serial_parameters_string;
	if (strncmp(p, "9600:", 5) == 0) dcbSerialParams.BaudRate = CBR_9600; // BaudRate = 9600
	else if (strncmp(p, "4800:", 5) == 0) dcbSerialParams.BaudRate = CBR_4800; // BaudRate = 4800
	else if (strncmp(p, "2400:", 5) == 0) dcbSerialParams.BaudRate = CBR_2400; // BaudRate = 2400
	else if (strncmp(p, "1200:", 5) == 0) dcbSerialParams.BaudRate = CBR_1200; // BaudRate = 1200
	else {
		wprintf(L"Invalid serial speed\r\n");
		CloseHandle(hComm);
		exit(1);
	}

	p = &(pg->serial_params[5]);
	if (*p == '7') dcbSerialParams.ByteSize = 7;
	else if (*p == '8') dcbSerialParams.ByteSize = 8;
	else {
		wprintf(L"Invalid serial byte size '%c'\r\n", *p);
		CloseHandle(hComm);
		exit(1);
	}

	p++;
	if (*p == 'o') dcbSerialParams.Parity = ODDPARITY;
	else if (*p == 'e') dcbSerialParams.Parity = EVENPARITY;
	else if (*p == 'n') dcbSerialParams.Parity = NOPARITY;
	else {
		wprintf(L"Invalid serial parity type '%c'\r\n", *p);
		CloseHandle(hComm);
		exit(1);
	}
}
#endif

/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220307
  Function Name	: main
  Returns Type	: int
  ----Parameter List
  1. int argc,
  2.  char **argv ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int main ( int argc, char **argv ) {

	SDL_Event event;
	SDL_Surface *surface, *surface_2;
	SDL_Texture *texture, *texture_2;

	char linetmp[SSIZE]; // temporary string for building main line of text

	struct glb g;        // Global structure for passing variables around
	int i = 0;           // Generic counter
	char temp_char;        // Temporary character
	char tfn[4096];
	bool quit = false;
	int flag_ol = 0;

	glbs = &g;

	/*
	 * Initialise the global structure
	 */
	init(&g);

	/*
	 * Parse our command line parameters
	 */
	parse_parameters(&g, argc, argv);
	if (g.device == NULL && g.serial_parameters_string[0] == '\0') {
		fprintf(stdout,"Require valid device (ie, -p /dev/usbtmc2 )\nExiting\n");
		exit(1);
	}

	fprintf(stdout,"START\n");

	if ((g.device != NULL) && strstr(g.device,"usbtmc")) {
		fprintf(stdout,"\nUsing USB mode\n\n");
		fflush(stdout);
		g.comms_mode = CMODE_USB;
		snprintf(g.meas_value,sizeof(g.meas_value), "%s\n", MEAS_VALUE);
		snprintf(g.get_func,sizeof(g.get_func), "%s\n", GET_FUNC);
	} else {
		fprintf(stdout,"\nUsing SERIAL mode\n\n");
		fflush(stdout);
		g.comms_mode = CMODE_SERIAL;
		g.serial_params.device = g.device;
		snprintf(g.meas_value,sizeof(g.meas_value), "%s\n", MEAS_VALUE);
		snprintf(g.get_func,sizeof(g.get_func), "%s\n", GET_FUNC);
	}

	/* 
	 * check paramters
	 *
	 */
	if (g.font_size < 10) g.font_size = 10;
	if (g.font_size > 200) g.font_size = 200;

	if (g.output_file) snprintf(tfn,sizeof(tfn),"%s.tmp",g.output_file);


	if ( g.comms_mode == CMODE_SERIAL ) {
		/* 
		 * handle the serial port
		 *
		 */
		open_port( &g );

	} else {
		/*
		 * Handle the USB port
		 *
		 */

		g.usb_fhandle = open( g.device, O_RDWR );
		if (g.usb_fhandle == -1) {
			fprintf(stdout, "Error opening device [%s] : %s\n", g.device, strerror(errno));
			exit (1);
		}
	}

	/*
	 * Setup SDL2 and fonts
	 *
	 */

	SDL_Init(SDL_INIT_VIDEO);
	TTF_Init();
	TTF_Font *font = TTF_OpenFont("RobotoMono-Regular.ttf", g.font_size);
	TTF_Font *font_small = TTF_OpenFont("RobotoMono-Regular.ttf", g.font_size/4);

	/*
	 * Get the required window size.
	 *
	 * Parameters passed can override the font self-detect sizing
	 *
	 */
	TTF_SizeText(font, " 00.00000V ", &g.window_width, &g.window_height);
	g.window_height *= 1.85;

	if (g.wx_forced) g.window_width = g.wx_forced;
	if (g.wy_forced) g.window_height = g.wy_forced;

	SDL_Window *window = SDL_CreateWindow("MP730424", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, g.window_width, g.window_height, 0);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
	if (!font) {
		fprintf(stderr,"Error trying to open font :( \r\n");
		exit(1);
	}

	/* Select the color for drawing. It is set to red here. */
	SDL_SetRenderDrawColor(renderer, g.background_color.r, g.background_color.g, g.background_color.b, 255 );

	/* Clear the entire screen to our selected color. */
	SDL_RenderClear(renderer);

	/*
	 *
	 * Parent will terminate us... else we'll become a zombie
	 * and hope that the almighty PID 1 will reap us
	 *
	 */
	while (!quit) {
		char line1[1024];
		char line2[1024];
		char buf_func[100];
		char buf_value[100];
		char buf_units[10];
		char buf_multiplier[10];

		char *p, *q;
		double td = 0.0;
		double v = 0.0;
		int ev = 0;
		uint8_t range;
		uint8_t dpp = 0;
		ssize_t bytes_read = 0;
		ssize_t sz;


		while (SDL_PollEvent(&event)) {
			switch (event.type)
			{
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_q) quit = true;
					break;
				case SDL_QUIT:
					quit = true;
					break;
			}
		}

		linetmp[0] = '\0';


		/*
			if (g.error_flag) {
			f = open( g.device, O_RDWR );
			if (f == -1) {
			fprintf(stdout, "Error opening device [%s] : %s\n", g.device, strerror(errno));
			sleep(1);
			} else {
			error_flag = false;
			}

			}
			*/

		sz = data_write( &g, g.get_func, strlen(g.get_func)  );
		if (sz > -1) {
			sz = data_read( &g, buf_func, sizeof(buf_func) );
			if (sz > -1) {
				if (g.debug) fprintf(stdout,"function read: '%s'\n", buf_func);
				if (strstr(buf_func,"E+") || strstr(buf_func,"E-")) sz = data_read( &g, buf_func, sizeof(buf_func) );
			} else { 
				snprintf(buf_func,sizeof(buf_func),"NO DATA");
			}
		} else { 
			snprintf(buf_func,sizeof(buf_func),"NO DATA");
		}

		sz = data_write( &g, g.meas_value, strlen(g.meas_value));
		if (sz > -1) {
			sz = data_read( &g, buf_value, sizeof(buf_value));
			if (sz > -1) {
				if (g.debug) fprintf(stdout,"value read: '%s'\n", buf_value);
			} else {
				snprintf(buf_value,sizeof(buf_value),"NO DATA");
			}
		} else {
			snprintf(buf_value,sizeof(buf_value),"NO DATA");
		}

		/*
		 *
		 * END OF DATA ACQUISITION
		 *
		 */

		if ( strlen( buf_func ) == 0 || strlen( buf_value ) == 0 ) {
			snprintf(line1, sizeof(line1), "NO DATA");
			snprintf(line2, sizeof(line2), "* * * *");
		}

		else {
			if (strncmp(buf_value, "1E+9", 4)==0) {
				snprintf(line1,sizeof(line1),"Overlimit");
				flag_ol = 1;
			} else {
				char *e;
				flag_ol = 0;
				td = strtod(buf_value,NULL);
				snprintf(line1, sizeof(line1), "%f %s", td, buf_units);
				e = strstr(buf_value,"E+");
				if (!e) e = strstr(buf_value, "E-");
				if (e) {
					ev = strtol(e+2, NULL, 10);
				}
				if (g.debug) fprintf(stdout,"E = %d\n", ev);
			}
			if (g.debug) fprintf(stdout,"%s\n%s\n", line1, line2);
		}


		if (strncmp("\"DIOD",buf_func, 5)==0) {
			//
			// Work around for firmware fault
			//
			snprintf(buf_func,sizeof(buf_func), "Continuity");
			snprintf(buf_units,sizeof(buf_units) ,"%s", oo);
		} 
		else if (strncmp("\"CONT",buf_func, 5)==0) {
			//
			// Work around for firmware fault
			//
			snprintf(buf_func,sizeof(buf_func), "Diode");
			snprintf(buf_units,sizeof(buf_units) ,"V");
		} 
		else if (strncmp("\"VOLT AC\"",buf_func, 6)==0) {
			snprintf(buf_func,sizeof(buf_func), "Volts");
			snprintf(buf_units,sizeof(buf_units) ,"VAC");
		} 
		else if (strncmp("\"VOLT\"",buf_func, 6)==0) {
			snprintf(buf_func,sizeof(buf_func), "Volts");
			snprintf(buf_units,sizeof(buf_units) ,"VDC");
		} 
		else if (strncmp("\"CURR",buf_func, 5)==0) {
			snprintf(buf_func,sizeof(buf_func), "Current");
			snprintf(buf_units,sizeof(buf_units) ,"A");
		} 
		else if (strncmp("\"RES",buf_func, 4)==0) {
			snprintf(buf_func,sizeof(buf_func), "Resistance");
			snprintf(buf_units,sizeof(buf_units) ,"%s", oo);
		} 
		else if (strncmp("\"CAP",buf_func, 4)==0) {
			snprintf(buf_func,sizeof(buf_func), "Capacitance");
			snprintf(buf_units,sizeof(buf_units) ,"F");
		} 
		else if (strncmp("\"FREQ",buf_func, 5)==0) {
			snprintf(buf_func,sizeof(buf_func), "Frequency");
			snprintf(buf_units,sizeof(buf_units) ,"Hz");
		} 
		else if (strncmp("\"TEMP",buf_func, 5)==0) {
			snprintf(buf_func,sizeof(buf_func), "Temperature");
			snprintf(buf_units,sizeof(buf_units) ,"%sC", dd);
		} 

		if (flag_ol == 1) snprintf(line1, sizeof(line1), "OL");

		if (flag_ol == 0) {
		switch (ev) {
			case -12:
			case -11:
			case -10:
				snprintf(buf_multiplier, sizeof(buf_multiplier), "n");
				td *= 1E+9;
				snprintf(line1, sizeof(line1), "% 0.3f%s%s", td, buf_multiplier, buf_units);
				break;


			case -9:
			case -8:
			case -7:
				snprintf(buf_multiplier, sizeof(buf_multiplier), "%s", uu);
				td *= 1E+6;
				snprintf(line1, sizeof(line1), "% 0.3f%s%s", td, buf_multiplier, buf_units);
				break;

			case -6:
			case -5:
			case -4:
				snprintf(buf_multiplier, sizeof(buf_multiplier), "m");
				td *= 1E+3;
				snprintf(line1, sizeof(line1), "% 0.3f%s%s", td, buf_multiplier, buf_units);
				break;

			case -3:
			case -2:
			case -1:
				snprintf(buf_multiplier, sizeof(buf_multiplier), " ");
				snprintf(line1, sizeof(line1), "% 0.3f%s%s", td, buf_multiplier, buf_units);
				//					td *= 1E+6;
				break;

			case 0:
			case 1:
			case 2:
				snprintf(buf_multiplier, sizeof(buf_multiplier), " ");
				//					td *= 1E+6;
				snprintf(line1, sizeof(line1), "% 0.3f%s%s", td, buf_multiplier, buf_units);
				break;

			case 3:
			case 4:
			case 5:
				snprintf(buf_multiplier, sizeof(buf_multiplier), "k");
				td /= 1E+3;
				snprintf(line1, sizeof(line1), "% 0.3f%s%s", td, buf_multiplier, buf_units);
				break;

			case 6:
			case 7:
			case 8:
			case 9:
				snprintf(buf_multiplier, sizeof(buf_multiplier), "M");
				td /= 1E+6;
				snprintf(line1, sizeof(line1), "% 0.3f%s%s", td, buf_multiplier, buf_units);
				break;

		}
		}

		snprintf(line2, sizeof(line2), "%s", buf_func );



		{
			int texW = 0;
			int texH = 0;
			int texW2 = 0;
			int texH2 = 0;
			SDL_RenderClear(renderer);
			surface = TTF_RenderUTF8_Solid(font, line1, g.font_color_volts);
			texture = SDL_CreateTextureFromSurface(renderer, surface);
			SDL_QueryTexture(texture, NULL, NULL, &texW, &texH);
			SDL_Rect dstrect = { 0, 0, texW, texH };
			SDL_RenderCopy(renderer, texture, NULL, &dstrect);

			surface_2 = TTF_RenderUTF8_Solid(font, line2, g.font_color_amps);
			texture_2 = SDL_CreateTextureFromSurface(renderer, surface_2);
			SDL_QueryTexture(texture_2, NULL, NULL, &texW2, &texH2);
			dstrect = { 0, texH -(texH /5), texW2, texH2 };
			SDL_RenderCopy(renderer, texture_2, NULL, &dstrect);

			SDL_RenderPresent(renderer);

			SDL_DestroyTexture(texture);
			SDL_FreeSurface(surface);
			if (1) {
				SDL_DestroyTexture(texture_2);
				SDL_FreeSurface(surface_2);
			}

			if (g.error_flag) {
				sleep(1);
			} else {
				usleep(g.interval);
			}


		}


		if (g.output_file) {
			/*
			 * Only write the file out if it doesn't
			 * exist. 
			 *
			 */
			if (!fileExists(g.output_file)) {
				FILE *f;
				fprintf(stderr,"%s:%d: output filename = %s\r\n", FL, g.output_file);
				f = fopen(tfn,"w");
				if (f) {
					fprintf(f,"%s", linetmp);
					fprintf(stderr,"%s:%d: %s => %s\r\n", FL, linetmp, tfn);
					fclose(f);
					rename(tfn, g.output_file);
				}
			}
		}

	} // while(1)

	if (g.comms_mode == CMODE_USB) {
		close(g.usb_fhandle);
	}

	TTF_CloseFont(font);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();

	return 0;

}
