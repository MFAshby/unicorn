use std::io;
use std::io::prelude::*;
use std::panic::{self,AssertUnwindSafe};
use std::thread;
use std::time::{Duration};
use std::env;
use std::process;

extern crate spidev;
use spidev::{Spidev, SpidevOptions, SpiModeFlags};

extern crate freetype;
use freetype::{Library};

extern crate ndarray;
use ndarray::prelude::*;

// Creates the Serial Peripheral Interface device with all the
// right settings for us
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

// Renders the text, returning the _columns_ required to do so
// Optionally actually write the pixels into the supplied buffer 
// (this will be used for a 2-pass approach, render, allocate buffer, rerender)
fn r_text(text: &str, mut buffer: Option<&mut Array3<u8>>) -> usize {
    // TODO this is probably not efficient, change it to use a single library instance
    let lib = Library::init().expect("Failed to init freetype library");

    // TODO make the font face a constant (or configurable)
    let face = lib.new_face("/usr/share/fonts/gnu-free/FreeMono.otf",0).expect("Failed to load font");

    // TODO make display height a constant (or configurable)
    face.set_pixel_sizes(0, 16).expect("Failed to set font pixel sizes");
    
    // Taken straight from the freetype examples...
    let mut pen_x = 0;
    let mut pen_y = 0;
    for (chr_ix, chr) in text.chars().enumerate() {
        face.load_char(chr as usize, freetype::face::LoadFlag::RENDER).expect("Failed to load character..");
        let glyph = face.glyph();
        let bm = glyph.bitmap();
        //let x_off = glyph.bitmap_left() as usize;
        //let y_off = glyph.bitmap_top() as usize;
        //println!("x_off {} y_off {}",x_off, y_off);
        let rows = bm.rows() as usize;
        let cols = bm.width() as usize;
        for row in 0_usize..rows {
            for col in 0_usize..cols {
                let pitch = bm.pitch() as usize;
                let b = bm.buffer();
                let val: u8 = b[col + row*pitch];
                let x = pen_x + col; // + x_off; 
                let y = pen_y + row;
                let r = panic::catch_unwind(AssertUnwindSafe(|| {
                    if let Some(ref mut buf) = buffer {
                    // Set all colour values the same right now, so white text
                        buf[(x,y,0)] = val;
                        buf[(x,y,1)] = val;
                        buf[(x,y,2)] = val;
                    }
                }));
                if !r.is_ok() {
                    println!("panicked writing to x{} y{}", x, y);
                }
            }
        }
        // Fractional 26.6 format, which practically means just divide by 64 to get the right width...
        let x_adv = glyph.advance().x as usize / 64;
        let y_adv = glyph.advance().y as usize / 64;
        println!("Character {} index {} starts x{} y{} ends x{} y{} rows {} cols {}", chr, chr_ix, pen_x, pen_y, pen_x + x_adv, pen_y + y_adv, rows, cols);
        pen_x += x_adv;         
        pen_y += y_adv; 
    }
    pen_x + 17 // add one full screen width on the end
}

fn render_text(text: &str) -> Array3<u8> {
    let width = r_text(text, None);
    // Make height & color values constants?
    let mut a = Array::zeros((width, 16, 3));
    r_text(text, Some(&mut a));
    a
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("Usage: unicorn <text>");
        process::exit(2);
    }
    let sleep = Duration::from_millis(50);

    let text_arr = render_text(&args[1]);
    // Render a portion to the Unicorn hat 
    let mut spi = create_spi().expect("Failed to create SPI device");

    let mut x_off = 0_usize;
    let mut buf = [0_u8; 769];

    loop {
        buf[0] = 0x72;
        for x in 0_usize..16 {
            for y in 0_usize..16 {
                buf[3 * (x + 16*y) + 1 + 0] = text_arr[(x  + x_off, y, 0)];
                buf[3 * (x + 16*y) + 1 + 1] = text_arr[(x  + x_off, y, 1)];
                buf[3 * (x + 16*y) + 1 + 2] = text_arr[(x  + x_off, y, 2)];
            }
        }
        spi.write(&buf).expect("Failed to write to SPI device!");
        thread::sleep(sleep);
        x_off = (x_off + 1) % (text_arr.len_of(Axis(0)) - 16);
    }
}

