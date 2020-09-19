extern crate spidev;
use std::io;
use std::io::prelude::*;
use std::vec::{Vec};
use spidev::{Spidev, SpidevOptions, SpiModeFlags};

extern crate freetype;
use freetype::{Library};

extern crate ctrlc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

// Type for the display buffer
const DISPLAY_WIDTH: usize = 16;
const DISPLAY_HEIGHT: usize = 16;
const DISPLAY_BYTES_PER_PIXEL: usize = 3;

const BUFFER_WIDTH: usize = 128;

fn create_spi() -> io::Result<Spidev> {
    let mut spi = Spidev::open("/dev/spidev0.0")?;
    let options = SpidevOptions::new()
        .bits_per_word(8)
        .max_speed_hz(9_000_000)
        .mode(SpiModeFlags::SPI_MODE_0)
        .build();
    spi.configure(&options)?;
    Ok(spi)
}

fn display_buffer_offset(x: usize, y: usize) -> usize {
	// 1 leading byte for frame 
	// 3 bytes / pixel, 
	// 16 pixels / line
	1 + (x * DISPLAY_BYTES_PER_PIXEL) + (y * DISPLAY_HEIGHT * DISPLAY_BYTES_PER_PIXEL)
}

fn display_buffer_set_pixel(display_buffer: &mut [u8; 769], x: usize, y: usize, r: u8, g: u8, b: u8) {
	if x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT {
		// Discard out of bounds writes
		return;
	}
	let offset = display_buffer_offset(x, y);
	display_buffer[offset + 0] = r;
	display_buffer[offset + 1] = g;
	display_buffer[offset + 2] = b;
}

fn display_buffer_debug(display_buffer: &DisplayBuf) {
    for y in 0..display_buffer.len() {
        for x in 0..display_buffer[0].len() {
            let (r, g, b) = display_buffer[y][x];
            print!("{:#04x}{:#04x}{:#04x}", r, g, b);
        }
        print!("\n");
    }
}

type DisplayBuf = [[(u8, u8, u8); BUFFER_WIDTH]; DISPLAY_HEIGHT];

struct State {

}

fn main() {
    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();
    ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    });
    // Image to render, bigger than the HAT, we'll scroll it
    let mut display_buffer : DisplayBuf = [[(0_u8, 0_u8, 0_u8); BUFFER_WIDTH]; DISPLAY_HEIGHT];

    let text = "Hey";
    let lib = Library::init().expect("Failed to init freetype library");
    let face = lib.new_face("/usr/share/fonts/gnu-free/FreeMono.otf",0).expect("Failed to load font");
    face.set_pixel_sizes(0, 16).expect("Failed to set font pixel sizes");
    let mut pen_x = 0;
    //let mut pen_y = 0;
    for chr in text.chars() {
        face.load_char(chr as usize, freetype::face::LoadFlag::RENDER).expect("Failed to load character..");
        let glyph = face.glyph();
        let bm = glyph.bitmap();
        let x_off = glyph.bitmap_left() as usize;
        //let y_off = glyph.bitmap_top() as usize;
        let rows = bm.rows() as usize;
        let cols = bm.width() as usize;
        for y in 0_usize..rows {
            for x in 0_usize..cols {
                let pitch = bm.pitch() as usize;
                let ix = x + y*pitch;
                let val: u8 = bm.buffer()[ix];
                //display_buffer_set_pixel(&mut display_buffer, x + x_off, y, val, val, val);
                display_buffer[y][pen_x + x + x_off]= (val, val, val);
            }
        }
        pen_x += (glyph.advance().x as usize / 64); // Fractional 26.6 format
        //pen_y += (glyph.advance().y as usize * 64); 
    }

    // TODO write a subset of the whole buffer to the device. 
    // TODO slide the window over the whole buffer over time
    let mut spi = create_spi().expect("Failed to create SPI device");
    display_buffer_debug(&display_buffer);
    while running.load(Ordering::SeqCst) {
        //spi.write(&display_buffer).expect("Failed to write to SPI device");
    }
}

