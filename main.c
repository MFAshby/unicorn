#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>

// For interaction with the unicorn via SPI
//#include <bcm2835.h>

// For sleep function and file handling
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// For SPI interaction
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

// Correctly handle interrupt signal and close gracefully
#include <signal.h>

// memcpy function lives here, and some string usefulness
#include <string.h>

// Freetype for fonts rendering
#include <ft2build.h>
#include FT_FREETYPE_H

// For timing
#include <time.h>

// 16x16x3 colour pixels, plus start of frame byte
// https://forums.pimoroni.com/t/c-example-for-unicorn-hat-hd/5149/2
#define DISPLAY_WIDTH 16
#define DISPLAY_HEIGHT 16
#define DISPLAY_BUF_SIZE 769
static char display_buffer[DISPLAY_BUF_SIZE];

// File descriptor for open SPI device
static int spidev;

// For calculating the offset into the display buffer for a position
int display_buffer_offset(uint8_t x, uint8_t y) {
	// 1 leading byte for frame 
	// 3 bytes / pixel, 
	// 16 pixels / line
	return 1 + (x * 3) + (y * 16 * 3) ;
}

// For setting a pixel value in the display buffer
void display_buffer_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
	if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
		// Discard out of bounds writes
		return;
	}
	uint16_t offset = display_buffer_offset(x, y);
	display_buffer[offset + 0] = r;
	display_buffer[offset + 1] = g;
	display_buffer[offset + 2] = b;
}

// Reset the buffer to empty, i.e. all pixels off
void display_buffer_reset(void) {
	memset(display_buffer, 0, sizeof(display_buffer));
	display_buffer[0] = 0x72;
}

// Send the current buffer to the device
void display_buffer_send(void) {
	//bcm2835_spi_writenb(display_buffer, sizeof(display_buffer));
	//ssize_t w = write(spidev, display_buffer, sizeof(display_buffer));

	//if (w != DISPLAY_BUF_SIZE) {
	//	printf("didn't write full buffer to display!, wrote %ld\n", w);
	//}

	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)display_buffer,
		//.rx_buf = (unsigned long)rx,
		.len = sizeof(display_buffer),
		.delay_usecs = 0,
		.speed_hz = 9000000,
		.bits_per_word = 8,
	};

	int ret = ioctl(spidev, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1) {
		printf("Can't send SPI message\n");
	}
}

// b/c sometimes this stuff goes wrong, just log it to the console!
/*
void print_debug_buffer() {
	for (int x=0;x<16; x++) {
		for (int y=0; y<16; y++) {
			int offset = display_buffer_offset(x, y);
			int r = display_buffer[offset];
			int g = display_buffer[offset+1];
			int b = display_buffer[offset+2];
			printf("(%02x,%02x,%02x)", r, g, b);
		}
		printf("\n");
	}
}
*/

static FT_Library library;
static FT_Face face;

int init_freetype2(void) {
	if (FT_Init_FreeType(&library)) {
		printf("Error initializing freetype library");
		return 1;
	}
	if (FT_New_Face(library,
                     "/usr/share/fonts/gnu-free/FreeMono.otf",
                     0,
                     &face)) {
		printf("Error loading arial.ttf\n");
		return 1;
	}
	if (FT_Set_Pixel_Sizes(
		  face,
		  0,
		  DISPLAY_HEIGHT)) {
		printf("Error setting font size to %dpx height", DISPLAY_HEIGHT);
		return 1;
	}
	return 0;
}

// Correctly handle interrupt signal
volatile sig_atomic_t stop; 
void siginthandler(int sig) {
	stop = 1;
}

// init the broadcom library for writing to SPI
// returns 0 on success, 1 on any error
/*
int init_bcm2835(void) {
	if (!bcm2835_init()) {
		printf("Failed to start SPI! Are you running as root?\n");
		return 1;
	}
	if (!bcm2835_spi_begin()) {
		printf("Failed to start SPI! Are you running as root?\n");
		return 1;
	}
	bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);      // The default
	bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);                   // The default
	bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_16);
	bcm2835_spi_chipSelect(BCM2835_SPI_CS0);                      // The default
	bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);      // the default
	return 0;
}

void uninit_bcm2835(void) {
	bcm2835_spi_end();
	bcm2835_close();
}
*/

void graphics_callback(uint64_t total_elapsed) {
	static const char* text = "Henry is the greatest!   ";
	static uint64_t last_character_change = 0;
	static uint16_t current_character_index = 0;

	uint64_t since_last_char_change = total_elapsed - last_character_change;
	// 500ms / character
	if (since_last_char_change < 500000000) {
		return;
	}
	last_character_change = total_elapsed;
	current_character_index ++;
	current_character_index = current_character_index % strlen(text);

	// Reset the display_buffer
	display_buffer_reset();

	// Set up text to render (this should come from websocket submission)
	FT_GlyphSlot slot = face->glyph;

	// Render characters to the display_buffer
	// int pen_x = 0;
	// int pen_y = 0;
	// for (int i=0; i<strlen(text); i++) {
		// Load the next character and render it
		if (FT_Load_Char(face, text[current_character_index], FT_LOAD_RENDER)) {
			printf("Error loading char!\n");
			return;
		}
		// Check the bitmap that's produced
		FT_Bitmap* bm = &slot->bitmap; 
		if (bm->pixel_mode != FT_PIXEL_MODE_GRAY) {
			printf("Unexpected pixel mode %d\n", bm->pixel_mode);
			return;
		}

		// Copy it to our display_buffer.
		int x_off = slot->bitmap_left;
		//int y_off = slot->bitmap_top;
		for (int y=0; y<bm->rows; y++) {
			for (int x=0; x<bm->width; x++ ) {
				char val = bm->buffer[x + (y*bm->pitch)];
				display_buffer_set_pixel(x + x_off, y, val, val, val);
			}
		}
		// advance pen position
		// pen_x += slot->advance.x >> 6;
		// pen_y += slot->advance.y >> 6;
	//}
	//print_debug_buffer();
}

uint64_t current_nanos() {
	static struct timespec t;
	if (clock_gettime(CLOCK_REALTIME, &t)) {
		printf("Error getting time!");
		exit(1);
	}
	return (t.tv_sec * 1000000) + t.tv_nsec;
}

// Sends the current display buffer to the device, rate-limiting to 120 fps
void display_buffer_send_callback(uint64_t total_elapsed) {
	static uint64_t lastframe = 0;
	uint64_t since_lastframe = total_elapsed - lastframe;
	uint64_t min_interval = 1000000000 / 120; // 120 fps
	if (since_lastframe < min_interval) {
		return;
	}
	lastframe = total_elapsed;
	display_buffer_send();
}

static void pabort(const char *s)
{
	perror(s);
	abort();
}

int main(void) {
	// hi
	printf("Hello, Unicorn!\n");

	// Set up graceful signal handling
	signal(SIGINT, siginthandler);

	// Set up broadcom library
	//if (init_bcm2835()) {
	//	return 1;
	//}
	spidev = open("/dev/spidev0.1", O_RDWR);
	if (spidev == -1) {
		printf("Failed to open /dev/spidev0.0\n");
		return 1;
	}

	/*
	 * spi mode
	 */
	uint8_t mode;
	uint8_t bits;
	uint32_t max_speed = 9000000;
	int ret = ioctl(spidev, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		pabort("can't set spi mode");

	ret = ioctl(spidev, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		pabort("can't get spi mode");

	/*
	 * bits per word
	 */
	ret = ioctl(spidev, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");

	ret = ioctl(spidev, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");

	/*
	 * max speed hz
	 */
	ret = ioctl(spidev, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed);
	if (ret == -1)
		pabort("can't set max speed hz");

	ret = ioctl(spidev, SPI_IOC_RD_MAX_SPEED_HZ, &max_speed);
	if (ret == -1)
		pabort("can't get max speed hz");

	printf("spi mode: %d\n", mode);
	printf("bits per word: %d\n", bits);
	printf("max speed: %d Hz (%d KHz)\n", max_speed, max_speed/1000);


	// Set up freetype library
	if (init_freetype2()) {
		return 1;
	}

	// Setup elapsed / since last frame timing
	uint64_t start = current_nanos();

	// Main loop time!
	while (!stop) {
		// Calculate elapsed times
		uint64_t c = current_nanos();
		uint64_t total_elapsed = c - start;

		// Do callbacks
		graphics_callback(total_elapsed);

		// Render
		display_buffer_send_callback(total_elapsed);
	}

	// Clear the display after finishing
	display_buffer_reset();
	display_buffer_send();
	usleep(1000000);

	// Clean up
	//uninit_bcm2835();
	close(spidev);
	return 0;
}
